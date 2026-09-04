#ifndef TURBOISO_H
#define TURBOISO_H

#include "graph.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <iomanip>
#include <cmath>

// 类型别名（与SUBAC0505q7.cpp保持一致）
using Node = int;
using Matches = std::vector<std::unordered_map<int, int>>;

// ========== 全局变量声明（与SUBAC0505q7.cpp保持一致） ==========

// 性能统计变量
extern int exploreCR_call_count;

// 节点数量
extern int numRemainNodes;
extern int numOriNodes;
extern int supernodenum;
extern int superedgenum;
extern int legalsuperedgesnum;

// 邻接表
extern std::vector<std::vector<int>> adjExternal;
extern std::vector<std::vector<int>> adjInternal;
extern std::vector<std::vector<int>> adjMerged;

// 超图结构
extern std::vector<SuperNode> superNodes;
extern std::vector<std::vector<int>> superNodeMembers;
extern std::vector<std::vector<int>> superAdj;

// 节点映射
extern std::vector<int> nodeToSuper;
extern std::vector<int> oldIdOfNew;

// GT（图编码）
extern std::vector<BitCode> gtCode;

// 标签和度数
extern std::vector<std::string> dataNodeLabel;
extern std::vector<int> globalDegree;

// GT命中指标
extern long long no_edge_and_gt_get;
extern long long no_edge_but_gt_noget;
extern long long have_edge_and_gt_noget;


// 匹配计数
extern long long matchtarget;
extern long long current_match_cnt;

// DFS性能分析全局变量
extern long long dfs_total_calls;           // DFS调用次数

// ExploreCR性能统计
extern double explorecr_time_ms;           // CR构建时间（毫秒）
extern long long explorecr_calls;          // ExploreCR递归调用次数

// ========== 结构体定义 ==========

// 顺序决定元素结构
struct Elem {
    int v;       // NEC 节点 ID
    double value; // 得分
};

// ========== 辅助函数声明 ==========

double comb(int n, int k);
int cmpElem(const void* a, const void* b);

// ========== NEC树相关函数 ==========

void FindNEC(
    vector<vector<int>>* NECV,
    vector<int>* vertexlist,
    vector<bool>* visited,
    int u,
    const vector<vector<int>>& adj
);

NECNode* buildNECTree(
    const QueryGraph& Q,
    int start_q,
    const vector<vector<int>>& qadj,
    const vector<NECNode*>& node2nec
);

void print_nec_tree(NECNode* node, int depth = 0);

// ========== 顺序决定函数 ==========

double DetermineMatchingOrder_MainCPP(
    NECNode* nec,
    CRTree* CR,
    const vector<vector<int>>& qadj,
    Elem* order,
    int product,
    const vector<NECNode*>& all_necs
);

void determineMatchOrder_MainCPP(
    NECNode* root,
    CRTree* CR,
    vector<int>& order,
    const vector<NECNode*>& all_necs,
    const QueryGraph& Q,
    const vector<vector<int>>& qadj,
    const vector<NECNode*>& node2nec
);

int chooseStartQVertex_TurboIso_runtime(const QueryGraph& Q);
void buildQueryAdj(const QueryGraph& Q, vector<vector<int>>& qadj);

// ========== CR相关函数 ==========

bool ExploreCR(
    int u_prime,
    vector<int>* VM,
    CRTree* CR,
    int parent_v,
    vector<NECNode*>& all_necs,
    const vector<vector<int>>& qadj,
    const QueryGraph& Q,
    vector<char>& visited
);

void ClearCR(int u, int vp, CRTree& CR);
void dumpCR(CRTree* CR, const vector<NECNode*>& all_necs);

// ========== 组合和排列生成 ==========

void generatePermutations(
    const vector<int>& arr,
    vector<vector<int>>& out
);

// ========== 状态管理 ==========

void UpdateState(
    vector<int>& M,
    vector<bool>& F,
    const vector<int>& qGroup,
    const vector<int>& combo
);

void RestoreState(
    vector<int>& M,
    vector<bool>& F,
    const vector<int>& qGroup,
    const vector<int>& combo
);

// ========== 连接性检查 ==========

bool isJoinable_usinghas_edge_in_data(
    int d,
    const vector<int>& neighbors_to_check
);

// ========== 主搜索函数 ==========

Matches subaa_execute(
    const QueryGraph& Q
);

void subgraphSearch_TurboIso_rec(
    const vector<int>& order,
    const vector<vector<int>>& qadj,
    const vector<NECNode*>& all_necs,
    const vector<NECNode*>& node2nec,
    vector<int>& M,
    vector<bool>& F,
    CRTree* CR,
    vector<unordered_map<int, int>>& outMatches,
    int depth
);

// ========== 查询图创建 ==========

// QueryGraph create_test_query_q0();// 4 clique
QueryGraph create_test_query_clique(int n);// 4 clique
QueryGraph create_test_query_q2();// 4 diamond
QueryGraph create_test_query_q3();// 5 diamond 1
QueryGraph create_test_query_q5();// 5 diamond 3
QueryGraph create_test_query_q6();// 6 diamond 3
QueryGraph create_test_query_q7();// 5 diamond 3 + 1

// ========== 工具函数 ==========

void PureInsert(vector<int>& vec, int x);
void print_matches(const vector<unordered_map<int, int>>& all_matches);

#endif // TURBOISO_H
