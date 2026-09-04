#ifndef TRICOUNT0_H
#define TRICOUNT0_H

#include <vector>
#include <string>
#include <unordered_map>
#include "graph.h"

using namespace std;

// 三角形计数结果结构
struct TriResult0 {
    long long internal = 0;      // 内部三角形（三个节点同一超节点）
    long long oneInTwoOut = 0;   // 一内二外（一个节点在超节点内，另两个在外）
    long long twoInOneOut = 0;   // 二内一外（两个节点在超节点内，一个在外）
    long long total() const { return internal + oneInTwoOut + twoInOneOut; }
};

// 三角形计数相关函数声明
void countTriangles0(const vector<SuperNode> &superNodes, TriResult0 &result);

#endif // TRICOUNT0_H
