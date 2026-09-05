#ifndef TURBOISO_H
#define TURBOISO_H

#include "graph.h"
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <chrono>
#include <iomanip>
#include <cmath>

// Type aliases
using Node = int;
using Matches = std::vector<std::unordered_map<int, int>>;

// ========== Global variable declarations ==========

// Performance counters
extern int exploreCR_call_count;

// Node counts
extern int numRemainNodes;
extern int numOriNodes;
extern int supernodenum;

// Adjacency lists
extern std::vector<std::vector<int>> adjExternal;
extern std::vector<std::vector<int>> adjInternal;
extern std::vector<std::vector<int>> adjMerged;

// Supergraph structure
extern std::vector<SuperNode> superNodes;
extern std::vector<std::vector<int>> superNodeMembers;
extern std::vector<std::vector<int>> superAdj;

// Node mappings
extern std::vector<int> nodeToSuper;
extern std::vector<int> oldIdOfNew;

// GT (graph encoding)
extern std::vector<BitCode> gtCode;

// Labels and degrees
extern std::vector<std::string> dataNodeLabel;
extern std::vector<int> globalDegree;

// GT hit metrics
extern long long no_edge_and_gt_get;
extern long long no_edge_but_gt_noget;
extern long long have_edge_and_gt_noget;


// Match counters
extern long long matchtarget;
extern long long current_match_cnt;

// DFS performance globals
extern long long dfs_total_calls;           // DFS call count

// ExploreCR performance
extern double explorecr_time_ms;           // CR build time (ms)
extern long long explorecr_calls;          // ExploreCR recursive call count

// ========== Struct definitions ==========

// Element for match-order decision
struct Elem {
    int v;       // NEC node id
    double value; // score
};

// ========== Helper function declarations ==========

double comb(int n, int k);
int cmpElem(const void* a, const void* b);

// ========== NEC tree functions ==========

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

// ========== Match-order functions ==========

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

// ========== CR functions ==========

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

// ========== Combination and permutation generation ==========

void generatePermutations(
    const vector<int>& arr,
    vector<vector<int>>& out
);

// ========== State management ==========

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

// ========== Connectivity checks ==========

bool isJoinable_usinghas_edge_in_data(
    int d,
    const vector<int>& neighbors_to_check
);

// ========== Main search functions ==========

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

// ========== Query graph creation ==========

QueryGraph create_test_query_clique(int n);// 4 clique
QueryGraph create_test_query_q2();// 4 diamond
QueryGraph create_test_query_q3();// 5 diamond 1
QueryGraph create_test_query_q5();// 5 diamond 3
QueryGraph create_test_query_q6();// 6 diamond 3
QueryGraph create_test_query_q7();// 5 diamond 3 + 1

// ========== Utility functions ==========

void PureInsert(vector<int>& vec, int x);
void print_matches(const vector<unordered_map<int, int>>& all_matches);

#endif // TURBOISO_H
