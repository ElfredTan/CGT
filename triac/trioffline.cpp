#pragma GCC optimize(3,"Ofast","inline")
#include "trioffline.h"
#include "graph.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;

// ========== Triangle counting ==========

// Composite edge query (unified with matching has_edge_in_data / cdac isNeighbor)
// Internal edges (same supernode): by shape — clique all true / star & diamond via synopsis slots / path via O(1) path_pos
// (nogt baseline: no GT, no gtbin dependency)
// External edges: adjExternal binary search (full edges, sorted)
bool hasEdge(int u, int v) {
    if (u == v) return false;
    if (nodeToSuper[u] == nodeToSuper[v]) {
        int sid = nodeToSuper[u];
        if (sid >= firstSingleId) return false;   // single has one member, no internal edges
        const SuperNode &S = superNodes[sid];
        if (S.type == 1) return true;         // clique: every internal pair adjacent
        if (S.type == 2)                      // star: center-leaf adjacent
            return u == S.beginnode || v == S.beginnode;
        if (S.type == 3)                      // diamond: begin/end adjacent to every member
            return u == S.beginnode || u == S.endnode ||
                   v == S.beginnode || v == S.endnode;
        if (S.type == 4)                      // path: |Δpos|==1 (path_pos O(1))
            return g_path_pos_ok && queryPathPos((uint32_t)u, (uint32_t)v);
        return false;
    }
    // External edges: adjExternal binary search
    if (u < v) return binary_search(adjExternal[u].begin(), adjExternal[u].end(), v);
    return binary_search(adjExternal[v].begin(), adjExternal[v].end(), u);
}

// 1. Internal triangles (all three nodes in one supernode)
long long countInternal(int sid, const vector<SuperNode> &superNodes) {
    if (sid >= firstSingleId) return 0;  // single has one member, no internal triangle
    const SuperNode &S = superNodes[sid];
    if (S.type == 1) {  // clique
        long long n = static_cast<long long>(S.size);
        return n * (n - 1) * (n - 2) / 6; // C(n,3)
    }
    if (S.type == 3) {  // diamond
        return 2;
    }
    // star/path have no internal triangles: leaf-leaf / non-consecutive pairs are non-adjacent
    return 0;
}

// 2. One-in-two-out (one node inside the supernode, the other two outside)
long long countOneInTwoOut(int sid, const vector<SuperNode> &superNodes) {
    long long res = 0;

    for (int u : superNodeMembers[sid]) {
        for (int neighborId : superAdj[sid]) {
            const SuperNode &C = superNodes[neighborId];
            if (neighborId >= firstSingleId || C.type != 1) continue;  // !=clique

            long long n = 0;
            for (int v : superNodeMembers[C.id]) {
                if (hasEdge(u, v)) n++;
            }
            if (n >= 2) {
                res += n * (n - 1) / 2; // C(n,2)
            }
        }
    }

    return res;
}

// 3. Two-in-one-out (two nodes inside the supernode, one outside)
long long countTwoInOneOut(int sid, const vector<SuperNode> &superNodes) {
    long long res = 0;

    if (sid >= firstSingleId) return 0;  // single: one member, no "two inside"
    const SuperNode &S = superNodes[sid];
    if (S.type == 4 || S.type == 1) return 0;  // path(4)/clique(1) lack the required adjacent pair
    // "two inside" needs an adjacent member pair: only star (center-leaf) / diamond (begin|end to any) qualify
    for (size_t i = 0; i < superNodeMembers[sid].size(); i++) {
        int u = superNodeMembers[sid][i];

        for (size_t j = i + 1; j < superNodeMembers[sid].size(); j++) {
            int v = superNodeMembers[sid][j];

            // unified composite query hasEdge (star/diamond branch semantics unchanged)
            if (!hasEdge(u, v)) continue;

            for (int nid : superAdj[sid]) {
                for (int w : superNodeMembers[nid]) {
                    if (hasEdge(u, w) && hasEdge(v, w)) {
                        res++;
                    }
                }
            }
        }
    }

    return res;
}

// Main counter: non-3-outside triangles (internal + one-in-two-out + two-in-one-out)
void countTriangles0(const vector<SuperNode> &superNodes, TriResult0 &result) {
    result.internal = 0;
    result.oneInTwoOut = 0;
    result.twoInOneOut = 0;

    for (int sid = 0; sid < (int)superNodes.size(); ++sid) {
        // 1. internal triangles
        result.internal += countInternal(sid, superNodes);

        // 2. one-in-two-out
        result.oneInTwoOut += countOneInTwoOut(sid, superNodes);

        // 3. two-in-one-out
        result.twoInOneOut += countTwoInOneOut(sid, superNodes);
    }
}

// ========== Main ==========

int main(int argc, char** argv) {
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <dataset_name>" << endl;
        return 1;
    }

    string dataset_name = argv[1];

    string output_prefix = "../data/output_" + dataset_name + "/";

    cout << "Dataset: " << dataset_name << endl;

    readSummaryStats((output_prefix + "summary.txt").c_str());
    cout << "From summary.txt: numRemainNodes: " << numRemainNodes
         << ", supernodenum: " << supernodenum
         << ", numOriNodes: " << numOriNodes << endl;
    cout << "numRemainNodes: " << numRemainNodes << endl;
    cout << "numOriNodes: " << numOriNodes << endl;

    adjExternal.resize(numOriNodes);
    // the unified loadExternalAdj fills globalDegree (subac semantics); tri ignores it but it must be allocated
    globalDegree.resize(numOriNodes);
    cout << "adjExternal size: " << adjExternal.size() << endl;
    cout << "supernodenum: " << supernodenum << endl;

    loadExternalAdj((output_prefix + "external_adj.txt").c_str());

    loadMappingTxt((output_prefix + "mapping.txt").c_str());
    loadMembersTxt((output_prefix + "members.txt").c_str());   // fills superNodeMembers (order-preserving, incl. singles)

    // O(1) intra-path edge structure (required by the hasEdge path branch)
    loadPathPos(output_prefix + "path_pos.txt");

    auto lines = readLines((output_prefix + "super_adj.txt").c_str());
    superAdj.resize(supernodenum);
    for (auto& line : lines) {
        if (line.empty()) continue;
        istringstream iss(line);
        int u;
        iss >> u;
        int v;
        while (iss >> v) {
            superAdj[u].push_back(v);
        }
    }

    superNodes = loadSuperGraph((output_prefix + "synopsis.txt").c_str());


    auto start = std::chrono::steady_clock::now();

    TriResult0 result;
    countTriangles0(superNodes, result);

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = end - start;

    cout << "--------------------------------------------------------------- " << "\n";
    cout << "Time taken: " << duration.count() << " seconds\n";

    cout << "--------------------------------------------------------------- " << "\n";
    cout << "Non-3-outside triangle counts:" << "\n";
    cout << "internal triangles (internal): " << result.internal << "\n";
    cout << "one-in-two-out (oneInTwoOut): " << result.oneInTwoOut << "\n";
    cout << "two-in-one-out (twoInOneOut): " << result.twoInOneOut << "\n";
    cout << "total: " << result.total() << "\n";
    cout << "---------------------------------------------------- " << "\n";

    return 0;
}
