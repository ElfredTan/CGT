#ifndef CDA_H
#define CDA_H

#include <vector>
#include <bitset>
#include "graph.h"

using namespace std;

// CDA global statistics
extern int k;

// GT prune statistics
extern long long no_edge_and_gt_get;
extern long long no_edge_but_gt_noget;
extern long long have_edge_and_gt_noget;


// DP prune statistics

// Timing statistics

// Supernode related
extern vector<SuperNode> superNodes;

// CDA core algorithm functions
bool isNeighbor(int u, int v);
bool dfsK(int sz, int num, int k);
bool solvesubK(vector<int> cliqueStart);
bool hasKClique(int k);
void printCDAStats();

#endif // CDA_H
