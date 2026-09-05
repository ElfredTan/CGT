#ifndef TRICOUNT_H
#define TRICOUNT_H

#include <vector>
#include <bitset>
#include "graph.h"

using namespace std;

// Triangle counting statistics
// extern long long superTriangleCountquery;
extern long long no_edge_and_gt_get;
extern long long no_edge_but_gt_noget;
extern long long have_edge_and_gt_noget;

// Globals
extern vector<BitCode> gtMask;

// Triangle counting functions
void tricount();
void resetTriStats();
void printTriStats();

#endif // TRICOUNT_H
