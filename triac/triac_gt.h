#ifndef TRICOUNT_H
#define TRICOUNT_H

#include <vector>
#include <bitset>
#include "graph.h"

using namespace std;

// 三角形计数全局统计变量
// extern long long superTriangleCountquery;
extern long long no_edge_and_gt_get;
extern long long no_edge_but_gt_noget;
extern long long have_edge_and_gt_noget;

// 全局变量
extern vector<BitCode> gtMask;

// 三角形计数相关函数
void tricount();
void resetTriStats();
void printTriStats();

#endif // TRICOUNT_H
