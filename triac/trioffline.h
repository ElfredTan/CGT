#ifndef TRICOUNT0_H
#define TRICOUNT0_H

#include <vector>
#include <string>
#include <unordered_map>
#include "graph.h"

using namespace std;

// Triangle counting result
struct TriResult0 {
    long long internal = 0;      // internal triangle (all three nodes in one supernode)
    long long oneInTwoOut = 0;   // one-in-two-out (one node inside, two outside)
    long long twoInOneOut = 0;   // two-in-one-out (two nodes inside, one outside)
    long long total() const { return internal + oneInTwoOut + twoInOneOut; }
};

// Triangle counting function declarations
void countTriangles0(const vector<SuperNode> &superNodes, TriResult0 &result);

#endif // TRICOUNT0_H

