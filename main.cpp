#include <iostream>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <array>
#include <unordered_set>
#include <iomanip>
#include <memory>
#include <cmath>
#include <cassert>
#include <chrono>
#include <limits>
#include <functional>
#include <bit>
#include <windows.h>
#include <fstream>
using namespace std;

inline unsigned long long splitmix64(unsigned long long x) {
   x += 0x9e3779b97f4a7c15;
   x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9;
   x = (x ^ (x >> 27)) * 0x94d049bb133111eb;
   return x ^ (x >> 31);
}

// Global trace for game-specific debugging
inline std::ofstream g_trace_file;
inline bool g_trace_enabled = false;


// ==================== 数据 ====================
struct GameState {
   enum class Cell : int {
      N0 = 0, N1 = 1, N2 = 2, N3 = 3, N4 = 4, N5 = 5, N6 = 6, N7 = 7, N8 = 8, H = 9
   };

   vector<vector<Cell>> board;    // 当前盘面
   vector<vector<bool>> flags;     // 旗帜标记
   int rows, cols, total_mines;

   GameState(int r, int c, int m) : rows(r), cols(c), total_mines(m) {
      board = vector<vector<Cell>>(r + 1, vector<Cell>(c + 1, Cell::H));
      flags = vector<vector<bool>>(r + 1, vector<bool>(c + 1, false));
   }
};

bool isdigit(GameState::Cell c) {
   int val = static_cast<int>(c);
   return 0 <= val && val <= 8;
}

template<typename Func>
void for_each_adjacent(int x, int y, int rows, int cols, Func&& func) {
   for (int i = -1; i <= 1; ++i) {
      for (int j = -1; j <= 1; ++j) {
         int nx = x + i, ny = y + j;
         if ((i != 0 || j != 0) && 1 <= nx && nx <= rows && 1 <= ny && ny <= cols) {
            func(nx, ny);
         }
      }
   }
}


// ==================== 推理结果 ====================
struct 基础逻辑结果 {
   enum class Mark { S, M, H, T };  // S=安全, M=危险, H=有信息未开, T=无信息未开

   vector<vector<Mark>> marks;
   int Tsum = 0, Msum = 0;

   基础逻辑结果(int rows, int cols) {
      marks = vector<vector<Mark>>(rows + 1, vector<Mark>(cols + 1, Mark::S));
      Tsum = 0, Msum = 0;
   }
};
struct 棋盘结构 {
   struct 连通块 {
      struct 单位格 {
         int size;
         vector<pair<int, int>> position;
      };
      struct 限制 {
         int sum = 0, x = 0, y = 0;
         vector<int> box_id;
         bool operator<(const 限制& other) const { if (sum != other.sum) return sum < other.sum; return box_id < other.box_id; }
         bool operator==(const 限制& other) const { return sum == other.sum && box_id == other.box_id; }
      };
      vector<单位格> 单位格们;
      vector<限制> 限制们;
   };
   vector<连通块> board_struct;
   vector<vector<连通块*>> cell2connect; // 每个格子对应的连通块指针列表
};
struct 连通块地雷分布 {
   struct 分布 {
      int mine_count; // 地雷数量
      long double ways; // 摆放方案数
      vector<long double> expect; // 每个单位格的期望值（雷数）
      struct 深度结果 {
         // 用于保存某个连通块所有可能的结果。
         struct 单个可能 {
            vector<char> assignment; // 每个 box 的雷数分配
            long double ways_perfix; // 该 assignment 的摆放方法数（组合数乘积）的前缀和
         };
         vector<单个可能> 摆放方式; // deepres 仅在 void build_probres 的 deep 为 true 时启用，因为会增加额外的计算量。
      };
      shared_ptr<深度结果> deepres = nullptr; // 深度结果指针，初始为 nullptr，表示未计算过
   };
   vector<分布> 分布们; // 每个地雷数量对应一个分布
};

struct 高级分析结果 {
   struct 连通块结果 {
      vector<long double> distrube_probability; // 每一个分布被取到的概率。
   };
   vector<连通块结果> full_result; // 每个连通块对应一个结果
   long double Tcell_probability = 0; // 非前沿是地雷的概率
   long double candidates = 0; // 候选方案数
};

struct 地雷概率 {
   vector<vector<long double>> probability;
};


// ==================== 算法 ====================
class 基础逻辑分析 {
   public:
   基础逻辑结果 analyze(const GameState& state) {
      基础逻辑结果 result(state.rows, state.cols);
      const auto& board = state.board;
      int n = state.rows, m = state.cols;

      // 1. 初始化：H格子设为T
      for (int i = 1; i <= n; ++i)
         for (int j = 1; j <= m; ++j)
            if (board[i][j] == GameState::Cell::H)
               result.marks[i][j] = 基础逻辑结果::Mark::T;

   // 2. 数字周围的H格子设为H
      for (int i = 1; i <= n; ++i)
         for (int j = 1; j <= m; ++j)
            if (isdigit(board[i][j]))
               for_each_adjacent(i, j, n, m, [&](int nx, int ny) {
               if (board[nx][ny] == GameState::Cell::H)
                  result.marks[nx][ny] = 基础逻辑结果::Mark::H;
            });

// 3. 雷的确定：数字 = 周围H数 → 设为M
      for (int i = 1; i <= n; ++i)
         for (int j = 1; j <= m; ++j)
            if (isdigit(board[i][j])) {
               int Hcnt = 0;
               for_each_adjacent(i, j, n, m, [&](int nx, int ny) {
                  if (board[nx][ny] == GameState::Cell::H) Hcnt++;
               });
               if (Hcnt == static_cast<int>(board[i][j]))
                  for_each_adjacent(i, j, n, m, [&](int nx, int ny) {
                  if (board[nx][ny] == GameState::Cell::H)
                     result.marks[nx][ny] = 基础逻辑结果::Mark::M;
               });
            }

    // 4. 安全的确定：数字 = 周围M数 → 剩余H设为S
      for (int i = 1; i <= n; ++i)
         for (int j = 1; j <= m; ++j)
            if (isdigit(board[i][j])) {
               int Mcnt = 0;
               for_each_adjacent(i, j, n, m, [&](int nx, int ny) {
                  if (result.marks[nx][ny] == 基础逻辑结果::Mark::M) Mcnt++;
               });
               if (Mcnt == static_cast<int>(board[i][j]))
                  for_each_adjacent(i, j, n, m, [&](int nx, int ny) {
                  if (board[nx][ny] == GameState::Cell::H &&
                      result.marks[nx][ny] != 基础逻辑结果::Mark::M) {
                     result.marks[nx][ny] = 基础逻辑结果::Mark::S;
                  }
               });
            }

    // 5. 统计
      result.Tsum = result.Msum = 0;
      for (int i = 1; i <= n; ++i)
         for (int j = 1; j <= m; ++j) {
            if (result.marks[i][j] == 基础逻辑结果::Mark::M) result.Msum++;
            if (result.marks[i][j] == 基础逻辑结果::Mark::T) result.Tsum++;
         }

      return result;
   }
};

class 连通块构造 {
   private:
   const GameState* state = nullptr;
   const 基础逻辑结果* basicresult = nullptr;
   vector<vector<bool>> vis;
   vector<vector<unsigned long long>> cell_hash; // 每个格子的哈希值，用于区分是否处在同一单位格
   vector<pair<int, int>> cell_list(int x, int y) { // 以一个 "H" 开始，搜索当前连通块，返回当前连通块的所有格子列表
      vector<pair<int, int>> result;
      auto dfs = [&](auto&& self, int cx, int cy) -> void {
         if (vis[cx][cy]) return;
         vis[cx][cy] = true;
         result.push_back({ cx, cy });

         if (isdigit(state->board[cx][cy])) {
            for_each_adjacent(cx, cy, state->rows, state->cols, [&](int nx, int ny) {
               if (basicresult->marks[nx][ny] == 基础逻辑结果::Mark::H)
                  self(self, nx, ny);
            });
         }

         if (basicresult->marks[cx][cy] == 基础逻辑结果::Mark::H) {
            for_each_adjacent(cx, cy, state->rows, state->cols, [&](int nx, int ny) {
               if (isdigit(state->board[nx][ny]))
                  self(self, nx, ny);
            });
         }
      };
      dfs(dfs, x, y);
      return result;
   }
   棋盘结构::连通块 build_connect(vector<pair<int, int>>& cell_list) { // 根据一个连通块的格子列表，构造出这个连通块的结构
      棋盘结构::连通块 result;

      vector<unsigned long long> hash_list;
      for (pair<int, int> i : cell_list)
         hash_list.push_back(cell_hash[i.first][i.second]);
      sort(hash_list.begin(), hash_list.end());
      hash_list.resize(unique(hash_list.begin(), hash_list.end()) - hash_list.begin());

      vector<int> hash_used(hash_list.size(), -1);

      for (pair<int, int> i : cell_list)
         if (basicresult->marks[i.first][i.second] == 基础逻辑结果::Mark::H) {
            unsigned long long hash_value = lower_bound(hash_list.begin(), hash_list.end(), cell_hash[i.first][i.second]) - hash_list.begin();
            if (hash_used[hash_value] == -1) {
               hash_used[hash_value] = (int)result.单位格们.size();
               result.单位格们.push_back({ 1,{{i.first,i.second}} });
            }
            else {
               result.单位格们[hash_used[hash_value]].size++;
               result.单位格们[hash_used[hash_value]].position.push_back({ i.first, i.second });
            }
            cell_hash[i.first][i.second] = hash_used[hash_value]; // 复用变量 cell_hash 用于保存 pos -> 单位格id 的映射
         }

      vector<bool> box_used(result.单位格们.size(), false);

      for (pair<int, int> i : cell_list)
         if (isdigit(state->board[i.first][i.second])) {
            result.限制们.push_back({ static_cast<int>(state->board[i.first][i.second]) ,i.first,i.second,{} });
            for_each_adjacent(i.first, i.second, state->rows, state->cols, [&](int nx, int ny) {
               if (basicresult->marks[nx][ny] == 基础逻辑结果::Mark::M)
                  result.限制们.back().sum--;
               if (basicresult->marks[nx][ny] == 基础逻辑结果::Mark::H) {
                  int box_id = (int)cell_hash[nx][ny];
                  if (box_used[box_id] == false) {
                     box_used[box_id] = true;
                     result.限制们.back().box_id.push_back(box_id);
                  }
               }
            });
            for (int j : result.限制们.back().box_id)
               box_used[j] = false;
         }
      return result;
   }
   public:
   棋盘结构 brute_build_struct(const GameState& State, const 基础逻辑结果& Basicresult) {
      // 根据当前棋盘状态和基础逻辑分析结果，构造出棋盘结构
      // 其实基础逻辑结果是不必要的，但是可以提高效率。
      vis = vector<vector<bool>>(State.rows + 1, vector<bool>(State.cols + 1, false));
      cell_hash = vector<vector<unsigned long long>>(State.rows + 1, vector<unsigned long long>(State.cols + 1, 0ull));
      state = &State;
      basicresult = &Basicresult;
      棋盘结构 result;
      result.cell2connect = vector<vector<棋盘结构::连通块*>>(State.rows + 1, vector<棋盘结构::连通块*>(State.cols + 1, nullptr));
      result.board_struct.clear();

      for (int i = 1; i <= state->rows; ++i)
         for (int j = 1; j <= state->cols; ++j)
            if (isdigit(state->board[i][j])) {
               unsigned long long seed = splitmix64(i * (state->cols + state->rows + 3) + j);
               for_each_adjacent(i, j, state->rows, state->cols, [&](int nx, int ny) {
                  cell_hash[nx][ny] += seed;
               });
            }
      for (int i = 1; i <= state->rows; ++i)
         for (int j = 1; j <= state->cols; ++j)
            if (basicresult->marks[i][j] == 基础逻辑结果::Mark::H && !vis[i][j]) {
               auto cells = cell_list(i, j);
               result.board_struct.push_back(build_connect(cells));
               for (auto i : cells)
                  result.cell2connect[i.first][i.second] = reinterpret_cast<棋盘结构::连通块*>(result.board_struct.size());
               // 这里看着存的是指针，但是实际上存的是下标，防止 vector 重新分配内存导致指针失效，后续会统一转换为真正的指针
            }
      for (int i = 1; i <= state->rows; ++i)
         for (int j = 1; j <= state->cols; ++j)
            if (result.cell2connect[i][j] != nullptr)
               result.cell2connect[i][j] = &result.board_struct.begin()[reinterpret_cast<size_t>(result.cell2connect[i][j]) - 1];
                  // 将 cell2connect 中的索引转换为指针
      return result;
   }
};

class 连通块地雷分布计算 {
   private:

   inline static unordered_map<unsigned long long, 连通块地雷分布> connect_map;
   // 计算一个连通块的哈希值，用于在 connect_map 中进行查找
   unsigned long long gen_hash(const 棋盘结构::连通块& Connect) {
      unsigned long long h = 0x19260817;
      // 哈希 box_list
      for (const auto& b : Connect.单位格们) {
         h ^= splitmix64(b.size);
         h = splitmix64(h);
      }
      // 哈希 limit_list
      for (const auto& lim : Connect.限制们) {
         h ^= splitmix64(lim.sum);
         for (int id : lim.box_id) {
            h ^= splitmix64((unsigned long long)id + 0x9e3779b9);
            h = splitmix64(h);
         }
         h = splitmix64(h);
      }
      return h;
   }
   public:
   连通块地雷分布 analysis(const 棋盘结构::连通块& Connect, bool deep) {
      // 根据一个连通块的结构，计算出这个连通块内每个单位格的地雷分布（地雷数量的期望值）
      连通块地雷分布 result;
      constexpr int MAXC = 9; // 编译期静态组合数表。
      constexpr array<array<long double, MAXC + 1>, MAXC + 1> C = []()->array<array<long double, MAXC + 1>, MAXC + 1> {
         array<array<long double, MAXC + 1>, MAXC + 1> arr{};
         for (int i = 0; i <= MAXC; ++i) {
            arr[i][0] = arr[i][i] = 1;
            for (int j = 1; j < i; ++j)
               arr[i][j] = arr[i - 1][j - 1] + arr[i - 1][j];
         }
         return arr;
      }();
      unsigned long long hsh = gen_hash(Connect);
      if (connect_map.count(hsh) && !deep) { // 通过哈希之前是否搜索过
         return connect_map[hsh];
      }

      int n = (int)Connect.单位格们.size(), max_total = 0;
      for (auto i : Connect.单位格们) max_total += i.size;

      vector<long double> waystable(max_total + 5, 0);
      vector<vector<long double>> expecttable(max_total + 5, vector<long double>(n, 0));
      vector<连通块地雷分布::分布::深度结果> deeprestable;
      vector<int> stk_idx(n + 5, 0), stk_k(n + 5, 0);
      vector<long double> stk_ways(n + 5, 0);
      vector<char> assignment(n + 5, char(0));
      if (deep) deeprestable = vector<连通块地雷分布::分布::深度结果>(max_total + 5);
      vector<vector<int>> table(n, vector<int>());
      for (int i = 0; i < (int)Connect.限制们.size(); ++i) { // 每个 lim 涉及到的最大 boxid，一旦搜索到了就直接判掉。
         int m = 0;
         for (int x : Connect.限制们[i].box_id) m = max(m, x);
         table[m].push_back(i);
      }

      int sp = 0;
      stk_idx[0] = 0;
      stk_k[0] = 0;
      stk_ways[0] = 1;


      while (sp >= 0) {
         int idx = stk_idx[sp];
         if (stk_k[sp] == 0) {
            if (idx == n) {
               int total = 0;
               for (int i = 0; i < n; ++i) total += assignment[i];
               if (deep) {
                  // 如果深度思考被打开，那么保存每个 total 下的 assignment 和对应的组合数（ways）
                  vector<char> asgn(assignment.begin(), assignment.begin() + n);
                  deeprestable[total].摆放方式.push_back({ asgn,stk_ways[sp] }); // 先存方案数，后面要计算前缀和。
               }
               waystable[total] += stk_ways[sp];
               for (int i = 0; i < n; ++i)
                  expecttable[total][i] += stk_ways[sp] * assignment[i];
               sp--;
               continue;
            }
         }
         if (stk_k[sp] > Connect.单位格们[idx].size) {
            sp--;
            continue;
         }
         int k = stk_k[sp]++;
         assignment[idx] = k;
         long double new_ways = stk_ways[sp] * C[Connect.单位格们[idx].size][k];
         bool ok = true;
         for (int limit_id : table[idx]) { // 只检查当前 box_id 刚好被搜索到的限制们
            int s = 0;
            for (int box_id : Connect.限制们[limit_id].box_id)
               s += assignment[box_id];
            if (s != Connect.限制们[limit_id].sum) {
               ok = false;
               break;
            }
         }
         if (ok) { // 继续搜索
            ++sp;
            stk_idx[sp] = idx + 1;
            stk_k[sp] = 0;
            stk_ways[sp] = new_ways;
         }
      }
      for (int total = 0; total <= max_total; ++total) {
         if (waystable[total] == 0) continue;
         result.分布们.push_back({});
         result.分布们.back().mine_count = total;
         result.分布们.back().ways = waystable[total];
         result.分布们.back().expect.swap(expecttable[total]);
         if (deep) {
            连通块地雷分布::分布::深度结果* tmp_ptr = new 连通块地雷分布::分布::深度结果;
            tmp_ptr->摆放方式.swap(deeprestable[total].摆放方式);
            result.分布们.back().deepres = shared_ptr< 连通块地雷分布::分布::深度结果>(tmp_ptr);
            for (int i = 1; i < (int)tmp_ptr->摆放方式.size(); ++i)
               tmp_ptr->摆放方式[i].ways_perfix += tmp_ptr->摆放方式[i - 1].ways_perfix; // 计算前缀和
         }
         else
            result.分布们.back().deepres = nullptr;
         for (int i = 0; i < n; ++i)
            result.分布们.back().expect[i] /= waystable[total];
      }
      if (!deep)
         connect_map[hsh] = result;
      return result;
   }
   vector<连通块地雷分布> analysis(const 棋盘结构& Structure, bool deep) {
      // 懒人式一键调用，如果没有很独特的理由的话，就用这个吧！
      vector<连通块地雷分布> result;
      for (const auto& block : Structure.board_struct)
         result.push_back(analysis(block, deep));
      return result;
   }
};
class 概率分析 {
   private:
   struct 随机生成结果 {
      struct 连通块结果 {
         int mine_count; // 这个连通块的地雷数量
      };
      vector<连通块结果> full_result; // 每个连通块对应一个结果
      int Tcell_minecount = 0; // 非前沿是地雷的数量
   };
   inline static vector<long double> log_fact; // 阶乘对数
   static void combi_init(int n) {
      if (log_fact.empty()) log_fact.push_back(0.0);
      while ((int)log_fact.size() <= n + 1)
         log_fact.push_back(log_fact.end()[-1] + log(log_fact.size()));
   }
   static long double binom(int n, int k) {
      if (k<0 || k>n) return 0;
      if (k == 0 || k == n) return 1;
      combi_init(n);
      return exp(log_fact[n] - log_fact[k] - log_fact[n - k]);
   }
   struct Polynomial { // 多项式类，用于计算概率生成函数
      int start; // e^start 表示多项式的最低次幂，coeffs[i] 表示 x^(start+i) 的系数。
      vector<long double> coeffs;
      Polynomial operator*(const Polynomial& other) const {
         // 多项式乘法
         int start = this->start + other.start, size = (int)coeffs.size() + (int)other.coeffs.size() - 1;
         vector<long double> res(size, 0.0);
         for (int i = 0; i < (int)coeffs.size(); ++i)
            for (int j = 0; j < (int)other.coeffs.size(); ++j)
               res[i + j] += coeffs[i] * other.coeffs[j];
         return { start,res };
      }
      Polynomial operator/(const Polynomial& other) const {
         // 多项式除法，返回商多项式。
         int start = this->start - other.start, size = max(0, (int)coeffs.size() - (int)other.coeffs.size() + 1);
         vector<long double> res(size, 0.0);
         vector<long double> rem(coeffs);
         long double other_leading = other.coeffs.back();
         for (int i = (int)rem.size() - 1; i >= (int)other.coeffs.size() - 1; --i) {
            if (abs(rem[i]) < 1e-12) continue; // 如果当前余数的最高次幂系数接近于 0，则跳过
            long double factor = rem[i] / other_leading;
            int quot_idx = i - ((int)other.coeffs.size() - 1);
            res[quot_idx] = factor;
            for (int j = 0; j < (int)other.coeffs.size(); ++j) {
               rem[i - j] -= factor * other.coeffs[other.coeffs.size() - 1 - j];
            }
         }
         return { start,res };
      }
      Polynomial(int s, const vector<long double>& c) : start(s), coeffs(c) {}
      Polynomial() :start(0), coeffs({}) {}
      Polynomial(const 连通块地雷分布& s) {
         if (s.分布们.empty()) {
            *this = Polynomial(0, { 1.0 });
            return;
         }
         int min_exp = s.分布们[0].mine_count, max_exp = min_exp;
         for (连通块地雷分布::分布 i : s.分布们) {
            min_exp = min(min_exp, i.mine_count);
            max_exp = max(max_exp, i.mine_count);
         }
         vector<long double> coeffs(max_exp - min_exp + 1, 0.0);
         for (连通块地雷分布::分布 i : s.分布们)
            coeffs[i.mine_count - min_exp] = i.ways;
         *this = Polynomial(min_exp, coeffs);
      }
   };
   Polynomial full_gf_poly(const vector<连通块地雷分布>& connect_distributions) {
      // 计算所有连通块的联合概率生成函数
      if (connect_distributions.empty()) return Polynomial(0, { 1.0 });
      Polynomial result(connect_distributions[0]);
      for (int i = 1; i < (int)connect_distributions.size(); ++i)
         result = result * Polynomial(connect_distributions[i]);
      return result;
   }
   long double denominator(const Polynomial& gf, int total_mines, int Tsum) {
      // 计算候选方案数
      long double result = 0.0;
      for (int i = 0; i < (int)gf.coeffs.size(); ++i) {
         int heavy_mines = gf.start + i, light_mines = total_mines - heavy_mines;
         if (light_mines >= 0 && light_mines <= Tsum)
            result += gf.coeffs[i] * binom(Tsum, light_mines);
      }
      return result;
   }
   高级分析结果 analysis(const vector<连通块地雷分布>& connect_distributions, int total_mines, int Tsum) {
      // 根据每个连通块的地雷分布，以及未被基础逻辑发现的的地雷数量和无信息格子数量，计算出每个连通块的最终结果（每个分布被取到的概率）和非前沿格子的雷概率。
      // 这个是给我内部用的，除非你知道你在干什么，否则请不要调用这个函数。
      高级分析结果 result;
      Polynomial P_H = full_gf_poly(connect_distributions);
      long double Denominator = denominator(P_H, total_mines, Tsum);
      long double light_prob = 0.0;
      for (int i = 0; i < (int)P_H.coeffs.size(); ++i) {
         int heavy_mines = P_H.start + i;
         int light_mines = total_mines - 1 - heavy_mines;
         if (light_mines >= 0 && light_mines <= Tsum - 1)
            light_prob += P_H.coeffs[i] * binom(Tsum - 1, light_mines);
      }
      light_prob /= Denominator;
      for (int i = 0; i < (int)connect_distributions.size(); ++i) {
         Polynomial P_i(connect_distributions[i]);
         Polynomial T_i = P_H / P_i;
         result.full_result.push_back({});
         for (const 连通块地雷分布::分布& value : connect_distributions[i].分布们) {
            int v = value.mine_count;
            long double w = value.ways, numerator = 0;
            for (int j = 0; j < (int)T_i.coeffs.size(); ++j) {
               int t_mines = T_i.start + j, light_mines = total_mines - v - t_mines;
               if (light_mines >= 0 && light_mines <= Tsum)
                  numerator += w * T_i.coeffs[j] * binom(Tsum, light_mines);
            }
            long double prob = numerator / Denominator;
            result.full_result.back().distrube_probability.push_back(prob);
         }
      }
      result.candidates = Denominator;
      result.Tcell_probability = light_prob;
      return result;
   }
   随机生成结果 randomAnalysis(const vector<连通块地雷分布>& connect_distributions, int total_mines, int Tsum, unsigned long long& seed) {
      // 均匀，随机地生成一个符合条件的分布，要求分布要有深度结果。
      随机生成结果 result;

      Polynomial P_H = full_gf_poly(connect_distributions);
      for (int i = 0; i < (int)connect_distributions.size(); ++i) {
         if (!connect_distributions[i].分布们.empty())
            if (connect_distributions[i].分布们[0].deepres == nullptr) {
               cerr << "调用 randomAlalysis 需要连通块分布的深度结果，但当前分布没有，请先使用 analysis 函数计算出深度结果。" << endl;
               assert(false);
            }
         long double denominator = 0.0;
         for (int i = 0; i < (int)P_H.coeffs.size(); ++i) {
            int heavy_mines = P_H.start + i, light_mines = total_mines - heavy_mines;
            if (light_mines >= 0 && light_mines <= Tsum) {
               denominator += P_H.coeffs[i] * binom(Tsum, light_mines);
            }
         }
         P_H = P_H / Polynomial(connect_distributions[i]);
         seed = splitmix64(seed);
         long double r = (seed & 0xFFFFFFFFFFFFF) * 1.0 / (1LL << 52);
         bool flag = false;
         for (const 连通块地雷分布::分布& value : connect_distributions[i].分布们) {
            int v = value.mine_count;
            long double w = value.ways, numerator = 0;
            for (int j = 0; j < (int)P_H.coeffs.size(); ++j) {
               int t_mines = P_H.start + j, light_mines = total_mines - v - t_mines;
               if (light_mines >= 0 && light_mines <= Tsum)
                  numerator += w * P_H.coeffs[j] * binom(Tsum, light_mines);
            }
            long double prob = numerator / denominator;
            r -= prob;
            if (r < 1e-7) {
               total_mines -= v;
               result.full_result.push_back({ v });
               flag = true;
               break;
            }
         }
         if (flag == false) {
            cerr << "在尝试生成局面时发生意外错误，程序终止。" << endl;
            assert(false);
         }
      }
      result.Tcell_minecount = total_mines;
      return result;
   }
   template<typename Callback>
   void all_distrubte_inside(const vector<连通块地雷分布>& connect_distributions, int total_mines, int Tsum, Callback callback) {
      // 产生所有分布，注意只是分布，而不是具体的局面。想要获得具体的局面还需要根据深度结果进行进一步的生成。
      vector<随机生成结果::连通块结果> current_counts;
      current_counts.reserve(connect_distributions.size());
      auto dfs = [&](int block_index, int remaining_mines, auto&& dfs_ref) -> void {
         if (block_index == (int)connect_distributions.size()) {
            if (remaining_mines >= 0 && remaining_mines <= Tsum) {
               随机生成结果 result;
               result.full_result = current_counts;
               result.Tcell_minecount = remaining_mines;
               callback(result);
            }
            return;
         }

         const auto& block = connect_distributions[block_index];

         for (const 连通块地雷分布::分布& v : block.分布们) {
            if (v.mine_count <= remaining_mines) {
               current_counts.push_back(随机生成结果::连通块结果{ v.mine_count });
               dfs_ref(block_index + 1, remaining_mines - v.mine_count, dfs_ref);
               current_counts.pop_back();
            }
         }
      };

      dfs(0, total_mines, dfs);
   }
   public:
   高级分析结果 analysis(const GameState& state, const 基础逻辑结果& basic, const 棋盘结构& structure, const vector<连通块地雷分布>& mine_distrube) {
      // 给出每个分布的概率结果的接口，这个接口是公用的。
      int mines = state.total_mines, Tsum = 0;
      for (int i = 1; i <= state.rows; ++i)
         for (int j = 1; j <= state.cols; ++j) {
            if (basic.marks[i][j] == 基础逻辑结果::Mark::M)
               mines--;
            if (basic.marks[i][j] == 基础逻辑结果::Mark::T)
               Tsum++;
         }
      return analysis(mine_distrube, mines, Tsum);
   }
   地雷概率 transfer(const GameState& state, const 基础逻辑结果& basic, const 棋盘结构& structure, const vector<连通块地雷分布>& mine_distrube, const 高级分析结果& advancedresult) {
      地雷概率 result;
      result.probability = vector<vector<long double>>(state.rows + 1, vector<long double>(state.cols + 1, 0.0));
      for (int i = 1; i <= state.rows; ++i)
         for (int j = 1; j <= state.cols; ++j) {
            if (basic.marks[i][j] == 基础逻辑结果::Mark::S) continue;
            if (basic.marks[i][j] == 基础逻辑结果::Mark::M) result.probability[i][j] = 1.0;
            if (basic.marks[i][j] == 基础逻辑结果::Mark::T) result.probability[i][j] = advancedresult.Tcell_probability;
         }
      for (int i = 0; i < (int)advancedresult.full_result.size(); ++i) {
         for (int j = 0; j < (int)advancedresult.full_result[i].distrube_probability.size(); ++j) {
            long double prob = advancedresult.full_result[i].distrube_probability[j];
            const auto& distribution = mine_distrube[i].分布们[j];
            for (int k = 0; k < (int)distribution.expect.size(); ++k) {
               long double expect = distribution.expect[k];
               for (pair<int, int> pos : structure.board_struct[i].单位格们[k].position) {
                  result.probability[pos.first][pos.second] += expect * prob / structure.board_struct[i].单位格们[k].size;
               }
            }
         }
      }
      return result;
   }
   地雷概率 gen_random(const GameState& state, const 基础逻辑结果& basic, const 棋盘结构& structure, const vector<连通块地雷分布>& mine_distrube, unsigned long long& seed) {
      // 均匀随机生成一个符合当前棋盘状态的雷分布，并返回每个格子是雷的概率（0或1）。
      unsigned long long local_seed = seed;
      地雷概率 result;
      result.probability = vector<vector<long double>>(state.rows + 1, vector<long double>(state.cols + 1, 0.0));
      for (int i = 1; i <= state.rows; ++i)
         for (int j = 1; j <= state.cols; ++j) {
            if (basic.marks[i][j] == 基础逻辑结果::Mark::S) continue;
            if (basic.marks[i][j] == 基础逻辑结果::Mark::M) result.probability[i][j] = 1.0;
         }
      int mines = state.total_mines, Tsum = 0;
      for (int i = 1; i <= state.rows; ++i)
         for (int j = 1; j <= state.cols; ++j) {
            if (basic.marks[i][j] == 基础逻辑结果::Mark::M)
               mines--;
            if (basic.marks[i][j] == 基础逻辑结果::Mark::T)
               Tsum++;
         }
      随机生成结果 res = randomAnalysis(mine_distrube, mines, Tsum, seed);
      for (int i = 0; i < (int)structure.board_struct.size(); ++i) {
         int choosed_idx = -1;
         for (int idx = 0; idx < (int)(mine_distrube[i].分布们.size()); ++idx)
            if (mine_distrube[i].分布们[idx].mine_count == res.full_result[i].mine_count) {
               choosed_idx = idx;
               break;
            }
         if (choosed_idx == -1) {
            cerr << "发生未知错误，程序终止。" << endl;
            cerr << "[LOG] " << endl;
            cerr << "SEED : " << local_seed << endl;
            cerr << "局面:" << endl;
            for (int x = 1; x <= state.rows; ++x) {
               for (int y = 1; y <= state.cols; ++y) {
                  if (state.flags[x][y]) cerr << 'F';
                  else if (state.board[x][y] == GameState::Cell::H) cerr << 'H';
                  else cerr << static_cast<int> (state.board[x][y]);

               }
               cerr << endl;
            }
            cerr << "分布：" << i << endl;
            cerr << "期望地雷数: " << res.full_result[i].mine_count << "但是没有找到" << endl;
            assert(false);
         }
         const 连通块地雷分布::分布& chosen = mine_distrube[i].分布们[choosed_idx];
         if (chosen.deepres == nullptr) {
            cerr << "调用 gen_random 需要连通块分布的深度结果，请先使用 analysis 函数计算出深度结果。" << endl;
            assert(false);
         }
         long double totalWays = chosen.ways;
         seed = splitmix64(seed);
         long double rnd = (seed & 0xFFFFFFFFFFFFF) * 1.0L / (1ULL << 52) * totalWays;
         size_t pick = (size_t)(lower_bound(chosen.deepres->摆放方式.begin(), chosen.deepres->摆放方式.end(), rnd,
                                [](const 连通块地雷分布::分布::深度结果::单个可能& elem, long double value) {return elem.ways_perfix < value; })
                                - chosen.deepres->摆放方式.begin());
         if (pick >= chosen.deepres->摆放方式.size()) pick = chosen.deepres->摆放方式.size() - 1;
         const vector<char>& assignment = chosen.deepres->摆放方式[pick].assignment; // 每个元素为对应 box 的地雷数
         for (int b = 0; b < (int)structure.board_struct[i].单位格们.size(); ++b) { // 终于定位到，每个单元格里应该放多少个，然后根据随机结果放。
            int k = static_cast<int>(assignment[b]);
            if (k <= 0) continue;
            const vector<pair<int, int>>& positions = structure.board_struct[i].单位格们[b].position;
            int n = (int)positions.size();
            for (int i = 0; i < n && k > 0; ++i) {
               seed = splitmix64(seed);
               if ((seed % (n - i)) < static_cast<unsigned long long>(k)) {
                   // 接受当前格子
                  result.probability[positions[i].first][positions[i].second] = 1.0L;
                  k--;
               }
            }
         }
      }
      if (res.Tcell_minecount > 0) {
         int k = res.Tcell_minecount;
         int remaining_T = Tsum;  // 剩余的T格子总数

         // 遍历所有格子，遇到T格子时以概率k/remaining_T决定是否选择
         for (int i = 1; i <= state.rows && k > 0; ++i) {
            for (int j = 1; j <= state.cols && k > 0; ++j) {
               if (basic.marks[i][j] == 基础逻辑结果::Mark::T) {
                   // 当前T格子，以k/remaining_T的概率选择
                  seed = splitmix64(seed);
                  if ((seed % remaining_T) < static_cast<unsigned long long>(k)) {
                      // 选择这个T格子
                     result.probability[i][j] = 1.0L;
                     k--;
                  }
                  remaining_T--;  // 无论是否选择，剩余T格子数减少
               }
            }
         }
      }
      return result;
   }
   void all_distrubte(const GameState& state, const 基础逻辑结果& basic, const 棋盘结构& structure, const vector<连通块地雷分布>& connect_distributions, void (*callback)(const 地雷概率&)) {
      // 此函数会枚举所有可能的局面，并对每个局面调用一次 callback。会指数爆炸而且我没有判断输入是否合法，所以用的时候记得判一下，免得整个程序卡死。
      // Determine remaining mines and Tsum (non-frontier hidden cells)
      int mines = state.total_mines;
      int Tsum = 0;
      for (int i = 1; i <= state.rows; ++i) {
         for (int j = 1; j <= state.cols; ++j) {
            if (basic.marks[i][j] == 基础逻辑结果::Mark::M) mines--;
            if (basic.marks[i][j] == 基础逻辑结果::Mark::T) Tsum++;
         }
      }

      // collect list of T cell positions for later enumeration
      vector<pair<int, int>> Tcells;
      for (int i = 1; i <= state.rows; ++i)
         for (int j = 1; j <= state.cols; ++j)
            if (basic.marks[i][j] == 基础逻辑结果::Mark::T)
               Tcells.emplace_back(i, j);

      // helper to enumerate combinations of k positions out of given positions and mark them in prob
      auto enum_choose_positions = [&](auto&& self, const vector<pair<int, int>>& positions, int start, int need, 地雷概率& prob, auto&& on_complete) -> void {
         if (need == 0) {
            on_complete();
            return;
         }
         int n = (int)positions.size();
         for (int i = start; i <= n - need; ++i) {
            auto p = positions[i];
            prob.probability[p.first][p.second] = 1.0L;
            self(self, positions, i + 1, need - 1, prob, on_complete);
            prob.probability[p.first][p.second] = 0.0L;
         }
      };

      // main callback from distribution-level enumeration: r contains per-block mine counts and Tcell_minecount
      auto tmp_function = [&](const 随机生成结果& rse) {
         // For each block, we need to iterate all deep assignments and then iterate per-box inner placements.
         int blocks = (int)connect_distributions.size();

         // For each block, prepare pointer to chosen 分布
         vector<const 连通块地雷分布::分布*> chosen_dist(blocks, nullptr);
         for (int i = 0; i < blocks; ++i) {
            int want = rse.full_result[i].mine_count;
            const auto& block = connect_distributions[i];
            int choose_idx = -1;
            for (int j = 0; j < (int)block.分布们.size(); ++j) {
               if (block.分布们[j].mine_count == want) { choose_idx = j; break; }
            }
            if (choose_idx == -1) return; // inconsistent, skip
            chosen_dist[i] = &block.分布们[choose_idx];
            if (chosen_dist[i]->deepres == nullptr) return; // deep results required
         }

         // recursive over blocks
         地雷概率 prob_template;
         prob_template.probability = vector<vector<long double>>(state.rows + 1, vector<long double>(state.cols + 1, 0.0L));
         // mark basic M as 1.0 and S as 0.0 (optional); we'll leave revealed numbers as 0
         for (int i = 1; i <= state.rows; ++i)
            for (int j = 1; j <= state.cols; ++j)
               if (basic.marks[i][j] == 基础逻辑结果::Mark::M) prob_template.probability[i][j] = 1.0L;

         // recursive over blocks and their deep assignments
         function<void(int, 地雷概率&)> dfs_block = [&](int bi, 地雷概率& cur_prob) {
            if (bi == blocks) {
               // handle T cells selection: choose rse.Tcell_minecount among Tcells
               int k = rse.Tcell_minecount;
               if (k == 0) {
                  callback(cur_prob);
                  return;
               }
               // enumerate combinations of Tcells
               enum_choose_positions(enum_choose_positions, Tcells, 0, k, cur_prob, [&]() { callback(cur_prob); });
               return;
            }

            const auto& deep = chosen_dist[bi]->deepres->摆放方式;
            // for each possible assignment (unit box counts) in this block
            for (const auto& poss : deep) {
               const vector<char>& assignment = poss.assignment;
               // enumerate placements inside boxes of this block
               地雷概率 copy_prob = cur_prob; // copy current state
               const auto& boxes = structure.board_struct[bi].单位格们;

               function<void(int)> dfs_box = [&](int boxi) {
                  if (boxi == (int)boxes.size()) {
                     // finished this block, go to next
                     dfs_block(bi + 1, copy_prob);
                     return;
                  }
                  int need = static_cast<int>(assignment[boxi]);
                  const auto& positions = boxes[boxi].position;
                  if (need == 0) {
                     dfs_box(boxi + 1);
                     return;
                  }
                  // choose `need` positions out of positions
                  enum_choose_positions(enum_choose_positions, positions, 0, need, copy_prob, [&]() { dfs_box(boxi + 1); });
               };

               dfs_box(0);
            }
         };

         地雷概率 start_prob = prob_template;
         dfs_block(0, start_prob);
      };

      // call distribution enumerator with computed mines and Tsum
      all_distrubte_inside(connect_distributions, mines, Tsum, tmp_function);
   }
};
class ZiniAlgo {
   private:
   vector<vector<bool>> need_open, bbv, Fl;
   vector<vector<int>> opening_id, hide_val, priority;

   public:

   template<bool randomize, bool fixed>
   pair<int, int> Gzini(const GameState& state, const 地雷概率& prob, int x = 0, int y = 0, unsigned long long* seed = nullptr) {
      int cls = 0;
      if constexpr (randomize) {
         if (seed == nullptr) {
            cerr << "oh wow，你是人啊？" << endl;
            assert(false);
         }
      }
      int R = state.rows, C = state.cols;
      hide_val = vector<vector<int>>(R + 1, vector<int>(C + 1, 0));
      opening_id = vector<vector<int>>(R + 1, vector<int>(C + 1, -1));
      need_open = vector<vector<bool>>(R + 1, vector<bool>(C + 1, true));
      bbv = vector<vector<bool>>(R + 1, vector<bool>(C + 1, true));
      priority = vector<vector<int>>(R + 1, vector<int>(C + 1, 0));
      Fl = state.flags;

      // neighbor offsets (8 neighbors)
      constexpr int dx[8] = { -1,-1,-1,0,0,1,1,1 };
      constexpr int dy[8] = { -1,0,1,-1,1,-1,0,1 };

      // 1) 初始化：对概率为 1 的格子，增加周围 hide_val，设置 bbv / need_open
      for (int i = 1; i <= R; ++i) {
         for (int j = 1; j <= C; ++j) {
            if (prob.probability[i][j] == 1.0L) {
               for (int d = 0; d < 8; ++d) {
                  int nx = i + dx[d], ny = j + dy[d];
                  if (nx >= 1 && nx <= R && ny >= 1 && ny <= C) hide_val[nx][ny]++;
               }
               bbv[i][j] = false;
               need_open[i][j] = false;
            }
            if (state.board[i][j] != GameState::Cell::H)
               need_open[i][j] = false;
         }
      }

      int ops = 0;
      // reuse a single flood buffer to avoid repeated allocations
      vector<pair<int, int>> flood;
      flood.reserve(512);

      // 2) 找空（flood fill）。在 push 时就标记 opening_id 为 -2，防止重复入队
      for (int i = 1; i <= R; ++i) {
         for (int j = 1; j <= C; ++j) {
            if (hide_val[i][j] == 0 && prob.probability[i][j] == 0.0L && opening_id[i][j] == -1) {
               ++ops;
               flood.clear();
               flood.push_back({ i, j });
               opening_id[i][j] = ops; // 标记为已入队
               while (!flood.empty()) {
                  auto cur = flood.back(); flood.pop_back();
                  int cx = cur.first, cy = cur.second;
                  for (int d = 0; d < 8; ++d) {
                     int nx = cx + dx[d], ny = cy + dy[d];
                     if (nx >= 1 && nx <= R && ny >= 1 && ny <= C) {
                        if (hide_val[nx][ny] == 0 && opening_id[nx][ny] == -1) {
                           opening_id[nx][ny] = ops;
                           flood.push_back({ nx, ny });
                        }
                     }
                  }
               }
            }
            if (prob.probability[i][j] == 1.0L) hide_val[i][j] = 9;
         }
      } // 找空

      // 3) 空边不是 bbv
      for (int i = 1; i <= R; ++i) {
         for (int j = 1; j <= C; ++j) {
            if (hide_val[i][j] == 0) {
               for (int d = 0; d < 8; ++d) {
                  int nx = i + dx[d], ny = j + dy[d];
                  if (nx >= 1 && nx <= R && ny >= 1 && ny <= C) {
                     if (hide_val[nx][ny] != 0) bbv[nx][ny] = false;
                  }
               }
            }
         }
      }

      // 3.5) 计算 bv，拿来做返回值用！
      int bv = 0;
      if constexpr (randomize == false && fixed == false) {
         // 如果开启随机化 / 固定首步采样的话，默认你要进行大量的计算了，所以此时会关闭 3Bv 计算，直接返回 0，避免不必要的性能损失。
         for (int i = 1; i <= R; ++i)
            for (int j = 1; j <= C; ++j)
               if (hide_val[i][j] != 0 && bbv[i][j]) bv++;

      }

      for (int i = 1; i <= R; ++i)
         for (int j = 1; j <= C; ++j)
            if (state.board[i][j] != GameState::Cell::H)
               bbv[i][j] = false;

      // 4) priority 计算
      for (int i = 1; i <= R; ++i) {
         for (int j = 1; j <= C; ++j) {
            if constexpr (fixed) {
               if (i == x && j == y) {
                  priority[i][j] = 10000; // 固定返回这个格子
                  continue;
               }
            }
            if (hide_val[i][j] == 0) {
               priority[i][j] = 0;
               continue;
            }
            priority[i][j]--; // chord 自带一下
            if (need_open[i][j]) priority[i][j]--; // 需要花费额外一次点击打开。
            if (prob.probability[i][j] == 1.0L && Fl[i][j] == false) {
               priority[i][j] = -10000;
               for (int d = 0; d < 8; ++d) {
                  int nx = i + dx[d], ny = j + dy[d];
                  if (nx >= 1 && nx <= R && ny >= 1 && ny <= C) priority[nx][ny]--;
               }
            }
            if (bbv[i][j] && hide_val[i][j] != 0) {
               for (int d = 0; d < 8; ++d) {
                  int nx = i + dx[d], ny = j + dy[d];
                  if (nx >= 1 && nx <= R && ny >= 1 && ny <= C) priority[nx][ny]++;
               }
            }
         }
      }

      vector<pair<int, int>> check[8]; // check[i] 储存优先级为 i 的格子。
      for (int i = 1; i <= R; ++i) {
         for (int j = 1; j <= C; ++j) {
            if (priority[i][j] >= 0) {
               int p = priority[i][j];
               if (p >= 8) p = 7; // 防止越界（虽然原逻辑不会出现）
               check[p].push_back({ i, j });
               if constexpr (randomize) {
                  *seed = splitmix64(*seed);
                  swap(check[p].back(), check[p][*seed % check[p].size()]);
               }
            }
         }
      }

      vector<vector<bool>> vis(R + 1, vector<bool>(C + 1, false));
      for (int maximum = 7; maximum >= 0; --maximum) {
         while (!check[maximum].empty()) {
            auto cur = check[maximum].back(); check[maximum].pop_back();
            int cx = cur.first, cy = cur.second;
            if (priority[cx][cy] != maximum) continue;
            if (need_open[cx][cy] == false && hide_val[cx][cy] == 0) continue;
            if (need_open[cx][cy]) {
               cls++;
               need_open[cx][cy] = false;
               if (bbv[cx][cy] && hide_val[cx][cy] != 0) {
                  for (int d = 0; d < 8; ++d) {
                     int nnx = cx + dx[d], nny = cy + dy[d];
                     if (nnx >= 1 && nnx <= R && nny >= 1 && nny <= C) {
                        priority[nnx][nny]--;
                        if (priority[nnx][nny] >= 0) {
                           check[priority[nnx][nny]].push_back({ nnx, nny });
                           if constexpr (randomize) {
                              *seed = splitmix64(*seed);
                              auto& p = check[priority[nnx][nny]];
                              swap(p.back(), p[*seed % p.size()]);
                           }
                        }
                        maximum = max(maximum, priority[nnx][nny]);
                     }
                  }
               }
            }

            // Flag 操作
            for (int d = 0; d < 8; ++d) {
               int nx = cx + dx[d], ny = cy + dy[d];
               if (nx >= 1 && nx <= R && ny >= 1 && ny <= C) {
                  if (prob.probability[nx][ny] == 1.0L && !Fl[nx][ny]) {
                     Fl[nx][ny] = true;
                     cls++;
                     for (int dd = 0; dd < 8; ++dd) {
                        int nnx = nx + dx[dd], nny = ny + dy[dd];
                        if (nnx >= 1 && nnx <= R && nny >= 1 && nny <= C) {
                           priority[nnx][nny]++;
                           if (priority[nnx][nny] >= 0) {
                              check[priority[nnx][nny]].push_back({ nnx, nny });
                              if constexpr (randomize) {
                                 *seed = splitmix64(*seed);
                                 auto& p = check[priority[nnx][nny]];
                                 swap(p.back(), p[*seed % p.size()]);
                              }
                           }
                           maximum = max(maximum, priority[nnx][nny]);
                        }
                     }
                  }
               }
            }

            if (hide_val[cx][cy] != 0) {
               cls++;
               // Chord 分支：遍历周围格子
               for (int d = 0; d < 8; ++d) {
                  int nx = cx + dx[d], ny = cy + dy[d];
                  if (nx < 1 || nx > R || ny < 1 || ny > C) continue;
                  if (!need_open[nx][ny]) continue;
                  need_open[nx][ny] = false;

                  priority[nx][ny]++; // 削弱没了。
                  maximum = max(maximum, priority[nx][ny]);
                  if (priority[nx][ny] >= 0) check[priority[nx][ny]].push_back({ nx,ny });

                  if (bbv[nx][ny] && hide_val[nx][ny] != 0) {
                     bbv[nx][ny] = false;
                     for (int dd = 0; dd < 8; ++dd) {
                        int nnx = nx + dx[dd], nny = ny + dy[dd];
                        if (nnx >= 1 && nnx <= R && nny >= 1 && nny <= C) {
                           priority[nnx][nny]--;
                           if (priority[nnx][nny] >= 0) {
                              check[priority[nnx][nny]].push_back({ nnx, nny });
                              if constexpr (randomize) {
                                 *seed = splitmix64(*seed);
                                 auto& p = check[priority[nnx][nny]];
                                 swap(p.back(), p[*seed % p.size()]);
                              }
                           }
                           maximum = max(maximum, priority[nnx][nny]);
                        }
                     }
                  }
                  else {
                     if (hide_val[nx][ny] == 0) {
                        // flood open starting from this neighbor. reuse flood buffer
                        flood.clear();
                        flood.push_back({ nx, ny });
                        while (!flood.empty()) {
                           auto cur2 = flood.back(); flood.pop_back();
                           int fx = cur2.first, fy = cur2.second;
                           if (need_open[fx][fy]) {
                              priority[fx][fy]++; // 削弱没了。
                              maximum = max(maximum, priority[fx][fy]);
                              if (priority[fx][fy] >= 0) check[priority[fx][fy]].push_back({ fx,fy });
                              need_open[fx][fy] = false;
                           }
                           bbv[fx][fy] = false;
                           for (int d2 = 0; d2 < 8; ++d2) {
                              int nnx = fx + dx[d2], nny = fy + dy[d2];
                              if (nnx < 1 || nnx > R || nny < 1 || nny > C) continue;
                              if (hide_val[nnx][nny] == 0 && need_open[nnx][nny]) {
                                 // mark and push
                                 need_open[nnx][nny] = false; // mark as processed to avoid duplicates
                                 priority[nnx][nny]++; // 削弱没了。
                                 maximum = max(maximum, priority[nnx][nny]);
                                 if (priority[nnx][nny] >= 0) check[priority[nnx][nny]].push_back({ nnx,nny });
                                 flood.push_back({ nnx, nny });
                              }
                              else {
                                 if (hide_val[nnx][nny] != 0) {
                                    if (need_open[nnx][nny]) {
                                       need_open[nnx][nny] = false;
                                       priority[nnx][nny]++; // 削弱没了。
                                       maximum = max(maximum, priority[nnx][nny]);
                                       if (priority[nnx][nny] >= 0) check[priority[nnx][nny]].push_back({ nnx,nny });
                                       vis[nnx][nny] = true;
                                    }
                                    else if (!vis[nnx][nny]) {
                                       vis[nnx][nny] = true;
                                       priority[nnx][nny]--;
                                       if (priority[nnx][nny] >= 0) {
                                          check[priority[nnx][nny]].push_back({ nnx, nny });
                                          if constexpr (randomize) {
                                             *seed = splitmix64(*seed);
                                             auto& p = check[priority[nnx][nny]];
                                             swap(p.back(), p[*seed % p.size()]);
                                          }
                                       }
                                    }
                                 }
                              }
                           }
                        }
                     }
                     else {
                        // chord 空边
                        priority[nx][ny]++;
                        if (priority[nx][ny] >= 0) {
                           check[priority[nx][ny]].push_back({ nx, ny });
                           if constexpr (randomize) {
                              *seed = splitmix64(*seed);
                              auto& p = check[priority[nx][ny]];
                              swap(p.back(), p[*seed % p.size()]);
                           }
                        }
                        maximum = max(maximum, priority[nx][ny]);
                     }
                  }
               }
            }
            else {
               // hide_val == 0 的情形
               flood.clear();
               flood.push_back({ cx, cy });
               while (!flood.empty()) {
                  auto cur2 = flood.back(); flood.pop_back();
                  int fx = cur2.first, fy = cur2.second;
                  need_open[fx][fy] = false;
                  priority[fx][fy]++; // 削弱没了。
                  maximum = max(maximum, priority[fx][fy]);
                  if (priority[fx][fy] >= 0) check[priority[fx][fy]].push_back({ fx,fy });
                  bbv[fx][fy] = false;
                  for (int d2 = 0; d2 < 8; ++d2) {
                     int nnx = fx + dx[d2], nny = fy + dy[d2];
                     if (nnx < 1 || nnx > R || nny < 1 || nny > C) continue;
                     if (hide_val[nnx][nny] == 0 && need_open[nnx][nny]) {
                        need_open[nnx][nny] = false;
                        priority[nnx][nny]++; // 削弱没了。
                        maximum = max(maximum, priority[nnx][nny]);
                        if (priority[nnx][nny] >= 0) check[priority[nnx][nny]].push_back({ nnx,nny });
                        flood.push_back({ nnx, nny });
                     }
                     else {
                        if (hide_val[nnx][nny] != 0) {
                           if (need_open[nnx][nny]) {
                              need_open[nnx][nny] = false;
                              priority[nnx][nny]++; // 削弱没了。
                              maximum = max(maximum, priority[nnx][nny]);
                              if (priority[nnx][nny] >= 0) check[priority[nnx][nny]].push_back({ nnx,nny });
                              vis[nnx][nny] = true;
                           }
                           else if (!vis[nnx][nny]) {
                              vis[nnx][nny] = true;
                              priority[nnx][nny]--;
                              if (priority[nnx][nny] >= 0) {
                                 check[priority[nnx][nny]].push_back({ nnx, nny });
                                 if constexpr (randomize) {
                                    *seed = splitmix64(*seed);
                                    auto& p = check[priority[nnx][nny]];
                                    swap(p.back(), p[*seed % p.size()]);
                                 }
                              }
                           }
                        }
                     }
                  }
               }

            }

         }
      }

      for (int i = 1; i <= R; ++i)
         for (int j = 1; j <= C; ++j)
            if (need_open[i][j]) {
               cls++;
            }
      return { cls,bv + ops };
   }

   int Rzini(const GameState& state, const 地雷概率& prob, unsigned long long& seed, int iter) {
      // RandomZini，专治各种疑难杂症局面。
      int best = 2147483647;
      for (int it = 0; it < iter; ++it) {
         auto pr = Gzini<true, false>(state, prob, 0, 0, &seed);
         if (pr.first < best) best = pr.first;
      }
      return best == 2147483647 ? -1 : best;
   }
   int Zini_8way(const GameState& state, const 地雷概率& prob) {
      // 对 8 个对称变换（D4）进行计算，取最小值
      int best = 2147483647;
      int R = state.rows, C = state.cols;
      for (int t = 0; t < 8; ++t) {
         int Rt = (t % 2 == 1 || t == 6 || t == 7) ? C : R; // for rot90/rot270/diag swap dims
         int Ct = (Rt == R) ? C : R;
         // more robust: set Rt,Ct per t
         switch (t) {
            case 0: Rt = R; Ct = C; break; // id
            case 1: Rt = C; Ct = R; break; // rot90
            case 2: Rt = R; Ct = C; break; // rot180
            case 3: Rt = C; Ct = R; break; // rot270
            case 4: Rt = R; Ct = C; break; // refl vertical
            case 5: Rt = R; Ct = C; break; // refl horizontal
            case 6: Rt = C; Ct = R; break; // refl main diag
            case 7: Rt = C; Ct = R; break; // refl anti diag
         }

         GameState ts(Rt, Ct, state.total_mines);
         // fill board and flags
         for (int i = 1; i <= R; ++i) {
            for (int j = 1; j <= C; ++j) {
               int ti = 1, tj = 1;
               switch (t) {
                  case 0: ti = i; tj = j; break; // id
                  case 1: ti = j; tj = R - i + 1; break; // rot90
                  case 2: ti = R - i + 1; tj = C - j + 1; break; // rot180
                  case 3: ti = C - j + 1; tj = i; break; // rot270
                  case 4: ti = i; tj = C - j + 1; break; // mirror vertical
                  case 5: ti = R - i + 1; tj = j; break; // mirror horizontal
                  case 6: ti = j; tj = i; break; // main diag
                  case 7: ti = C - j + 1; tj = R - i + 1; break; // anti diag
               }
               // assign
               if (ti >= 1 && ti <= Rt && tj >= 1 && tj <= Ct) {
                  ts.board[ti][tj] = state.board[i][j];
                  ts.flags[ti][tj] = state.flags[i][j];
               }
            }
         }

         // transform probabilities
         地雷概率 tprob;
         tprob.probability = vector<vector<long double>>(Rt + 1, vector<long double>(Ct + 1, 0.0L));
         for (int i = 1; i <= R; ++i) {
            for (int j = 1; j <= C; ++j) {
               int ti = 1, tj = 1;
               switch (t) {
                  case 0: ti = i; tj = j; break;
                  case 1: ti = j; tj = R - i + 1; break;
                  case 2: ti = R - i + 1; tj = C - j + 1; break;
                  case 3: ti = C - j + 1; tj = i; break;
                  case 4: ti = i; tj = C - j + 1; break;
                  case 5: ti = R - i + 1; tj = j; break;
                  case 6: ti = j; tj = i; break;
                  case 7: ti = C - j + 1; tj = R - i + 1; break;
               }
               if (ti >= 1 && ti <= Rt && tj >= 1 && tj <= Ct)
                  tprob.probability[ti][tj] = prob.probability[i][j];
            }
         }

         int val = Gzini<false, false>(ts, tprob).first;
         if (val < best) best = val;
      }
      return best == INT_MAX ? 0 : best;
   }

   int ZiNiDelta(const GameState& state, const 地雷概率& prob, int x, int y) {
      if (prob.probability[x][y] == 1.0L) return -100; // 已经是 1 的格子，点了就死了
      int zini_cur = Gzini<false, true>(state, prob).first;
      int zini_after = 0;
 // 没写完
      if (state.board[x][y] == GameState::Cell::H) { // 单击
         int hide_val = 0;
         for_each_adjacent(x, y, state.rows, state.cols, [&](int nx, int ny) {
            if (prob.probability[nx][ny] == 1.0L) hide_val++;
         });
         if (hide_val != 0) { // 不是 0 的话直接点开就好。
            GameState copyState = state;
            copyState.board[x][y] = static_cast<GameState::Cell>(hide_val);
            zini_after = Gzini<false, true>(copyState, prob).first + 1;
         }
         else zini_after = Gzini<false, true>(state, prob, x, y).first + 1; // 是 0 的话懒得处理了，直接传进去改一下优先级就能自己处理了。
      }
      else { // chord
         zini_after = Gzini<false, true>(state, prob, x, y).first;
      }
      return zini_cur - zini_after;
   }
};
class EffAlgo {
   public:
   struct 高ZNE下地雷分布 {
      int result_count;
      地雷概率 prob;
   };
   private:
   ZiniAlgo algo;
   template<typename Callback, bool shouldCallBack>
   高ZNE下地雷分布 地雷分布_inside(const GameState& state, const 基础逻辑结果& basic, const 棋盘结构& structure, const vector<连通块地雷分布>& mine_distrube
                  , unsigned long long& seed, int iter, long double Gzinilim, long double G10Zinilim, int clsDone, Callback callback=[]{}) {
      地雷概率 res_prob = { vector<vector<long double>>(state.rows + 1, vector<long double>(state.cols + 1, 0.0L)) };
      int result_cnt = 0;
      for (int i = 1; i <= iter; ++i) {
         地雷概率 random_result = 概率分析().gen_random(state, basic, structure, mine_distrube, seed);
         pair<int, int> zini_res = algo.Gzini<false, false>(state, random_result);
         if (zini_res.second * 1.0 / (zini_res.first + clsDone) < 1) {
            cerr << "该调 bug 了！" << endl;
            for (int i = 1; i <= state.rows; ++i) {
               for (int j = 1; j <= state.cols; ++j) {
                  cerr << (int)random_result.probability[i][j];
               }
               cerr << endl;
            }
            cerr << "bv = " << zini_res.second << ',' << "cls = " << zini_res.first + clsDone << endl;
            assert(false);
         }
         if (zini_res.second * 1.0 / (zini_res.first + clsDone) < Gzinilim) continue;
         int zini_10res = algo.Rzini(state, random_result, seed, 10);
         if (zini_res.second * 1.0 / (zini_10res + clsDone) < G10Zinilim) continue;
         // 找到一个高 ZNE 的结果了！把它加入统计
         result_cnt++;
         for (int x = 1; x <= state.rows; ++x)
            for (int y = 1; y <= state.cols; ++y)
               res_prob.probability[x][y] += random_result.probability[x][y];
         if constexpr (shouldCallBack)
            callback(random_result);
      }
      // 归一化（如果没有找到任何结果，则保持为 0）
      if (result_cnt > 0) {
         for (int x = 1; x <= state.rows; ++x)
            for (int y = 1; y <= state.cols; ++y)
               res_prob.probability[x][y] /= result_cnt;
      }
      return { result_cnt, res_prob };
   }
   地雷概率 ZNR表_inside(const GameState& state, const 基础逻辑结果& basic, const 棋盘结构& structure, const vector<连通块地雷分布>& mine_distrube
					, unsigned long long& seed, int iter, long double Effreq, int clsDone) {
      int cnt = 0; 
      auto callBack = [&](const 地雷概率& prob) { // 这里的 prob 是高 ZNE 排布
         cnt++;
         /*
         if (cnt < 20) {
            for(int i=1;i<=state.rows;++i)
               for (int j = 1; j <= state.cols; ++j) {
                  res.probability[i][j] += algo.ZiNiDelta(state, prob, i, j);
               }
              
         }*/
      }; 
      地雷分布_inside<decltype(callBack), true>(state, basic, structure, mine_distrube, seed, iter, Effreq * 0.8L, Effreq, clsDone, callBack);
   }
   public:
   高ZNE下地雷分布 地雷分布(const GameState& state, unsigned long long& seed, int iter, long double Gzinilim, long double G10Zinilim, int clsDone) {
      基础逻辑结果 basic = 基础逻辑分析().analyze(state);
      棋盘结构 棋盘结构_ = 连通块构造().brute_build_struct(state, basic);
      vector<连通块地雷分布> dists = 连通块地雷分布计算().analysis(棋盘结构_, true);
      return 地雷分布_inside<function<void()>,false>(state, basic, 棋盘结构_, dists, seed, iter, Gzinilim, G10Zinilim, clsDone);
   }
   地雷概率 ZNR表(const GameState& state,int iter, unsigned long long& seed, long double Effreq, int clsDone) {
      基础逻辑结果 basic = 基础逻辑分析().analyze(state);
      棋盘结构 棋盘结构_ = 连通块构造().brute_build_struct(state, basic);
      vector<连通块地雷分布> dists = 连通块地雷分布计算().analysis(棋盘结构_, true);
      return ZNR表_inside(state, basic, 棋盘结构_, dists, seed, iter, Effreq, clsDone);
   }
};
// ==================== 主求解器 ====================

// ==================== main ====================

int main() {
   ios::sync_with_stdio(false);
   cin.tie(nullptr);


   int n, m, mines; char t;
   if (!(cin >> m >> t >> n >> t >> mines)) return 0;

   int R = n, C = m;
   vector<string> rows;
   rows.reserve(R);
   for (int i = 0; i < R; ++i) {
      string s; cin >> s;
      if ((int)s.size() < C) s.append(C - (int)s.size(), 'H');
      rows.push_back(s);
   }

   GameState gs(R, C, mines);
   for (int i = 0; i < R; ++i) {
      for (int j = 0; j < C; ++j) {
         char ch = rows[i][j];
         if (ch == 'H' || ch == 'h') {
            gs.board[i + 1][j + 1] = GameState::Cell::H;
         }
         else if (ch == 'F' || ch == 'f') {
            gs.flags[i + 1][j + 1] = true;
         }
         else if (ch >= '0' && ch <= '8') {
            gs.board[i + 1][j + 1] = static_cast<GameState::Cell>(ch - '0');
         }
         else {
            gs.board[i + 1][j + 1] = GameState::Cell::H;
         }
      }
   }

   int clsDone = 0;
   if (!(cin >> clsDone)) clsDone = 0;

   unsigned long long seed = 123456789ULL;
   int iter = 10000;
   long double Gzinilim = 1.500L;
   long double G10Zinilim = 2.000L;

   EffAlgo eff;
   auto out = eff.地雷分布(gs, seed, iter, Gzinilim, G10Zinilim, clsDone);

   cout << "result_count: " << out.result_count << "\n";
   cout << fixed << setprecision(6);
   for (int i = 1; i <= R; ++i) {
      for (int j = 1; j <= C; ++j) {
         cout << out.prob.probability[i][j];
         if (j < C) cout << ' ';
      }
      cout << '\n';
   }

   return 0;
}

