#ifndef CDA_H
#define CDA_H

#include <vector>
#include <bitset>
#include "graph.h"

using namespace std;

// CDA 全局统计变量
extern int k;
extern int ans;
extern int maxcliquenumber;

// GT剪枝统计
extern long long no_edge_and_gt_get;
extern long long no_edge_but_gt_noget;
extern long long have_edge_and_gt_noget;


// DP剪枝统计变量

// 性能时间统计

// 超节点相关
extern vector<SuperNode> superNodes;

// CDA算法核心函数
bool isNeighbor(int u, int v);
bool dfsK(int sz, int num, int k);
bool solvesubK(vector<int> cliqueStart);
bool hasKClique(int k);
void printCDAStats();

#endif // CDA_H
