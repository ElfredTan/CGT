#pragma GCC optimize(3,"Ofast","inline")
#include "triac_gt.h"
#include "graph.h"
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace std;

// ========== Triangle counting statistics ==========

long long triangleCount = 0;
long long no_edge_and_gt_get = 0;
long long no_edge_but_gt_noget = 0;
long long have_edge_and_gt_noget = 0;

// ========== Triangle counting ==========

void resetTriStats() {
    triangleCount = 0;
    no_edge_and_gt_get = 0;
    no_edge_but_gt_noget = 0;
    have_edge_and_gt_noget = 0;
}

void printTriStats() {
    cout << "--------------------------------------------------------------- " << "\n";
    cout << "triangleCount: " << triangleCount << "\n";
#if RECORD_EDGE_STATS
    cout << "no_edge_and_gt_get: " << no_edge_and_gt_get << "\n";
    cout << "no_edge_but_gt_noget: " << no_edge_but_gt_noget << "\n";
    cout << "have_edge_and_gt_noget: " << have_edge_and_gt_noget << "\n";
#endif
    cout << "this is : gt (A-prune + neighbor-GT-prune, no mask-preprune; NO diag counters)" << "\n";
    cout << "---------------------------------------------------- " << "\n";
}

void tricount() {
    for (int i = 0; i < numOriNodes; i++) {
        int actualpointA = i;   // A: smallest triangle vertex

        int asize = static_cast<int>(adjExternal[actualpointA].size());
        if (!asize) continue;

        // mask prune (A): if the GT codes of all neighbors of A share a common bit (gtMask[A]!=0),
        // the neighbors of A form an independent set -> pairwise non-adjacent -> A is in no triangle, skip.
        // Lossless: the smallest vertex x of a triangle {x,y,z} (x<y<z) always has gtMask[x]==0.
        if (gtMask[actualpointA].any()) {
            continue;
        }

        auto itlegalbeginA = adjExternal[actualpointA].begin();
        auto itlegalendA = adjExternal[actualpointA].end();

        for (int j = 0; j < asize; j++) {
            int actualpointB = adjExternal[i][j];   // B: second vertex

            int bsize = static_cast<int>(adjExternal[actualpointB].size());
            if (!bsize) continue;

            // disjoint value ranges imply no common element between the two lists, skip
            if (adjExternal[actualpointA][asize - 1] < adjExternal[actualpointB][0] ||
                adjExternal[actualpointA][0] > adjExternal[actualpointB][bsize - 1])
                continue;

            auto itlegalbeginB = adjExternal[actualpointB].begin();
            auto itlegalendB = adjExternal[actualpointB].end();

            if (asize > bsize) {
                BitCode GTpointA = gtCode[actualpointA];

                for (auto itB = itlegalbeginB; itB != itlegalendB; ++itB) {
                    int neighborB = *itB;

                    if ((gtCode[neighborB] & GTpointA).any()) {
#if RECORD_EDGE_STATS
                        no_edge_and_gt_get++;
#endif
                        continue;
                    }

                    if (std::binary_search(itlegalbeginA, itlegalendA, neighborB)) {
                        triangleCount++;
#if RECORD_EDGE_STATS
                        have_edge_and_gt_noget++;
#endif
                    }
                }
            }
            else {
                BitCode GTpointB = gtCode[actualpointB];

                for (auto itA = itlegalbeginA; itA != itlegalendA; ++itA) {
                    int neighborA = *itA;

                    if ((gtCode[neighborA] & GTpointB).any()) {
#if RECORD_EDGE_STATS
                        no_edge_and_gt_get++;
#endif
                        continue;
                    }

                    if (std::binary_search(itlegalbeginB, itlegalendB, neighborA)) {
                        triangleCount++;
#if RECORD_EDGE_STATS
                        have_edge_and_gt_noget++;
#endif
                    }
                }
            }
        }
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
    string gtbin_path = "../data/gtbin/" + dataset_name + ".bin";

    cout << "Dataset: " << dataset_name << endl;

    readSummaryStats((output_prefix + "summary.txt").c_str());
    cout << "From summary.txt: numRemainNodes: " << numRemainNodes
         << ", supernodenum: " << supernodenum
         << ", numOriNodes: " << numOriNodes << endl;
    cout << "numRemainNodes: " << numRemainNodes << endl;
    cout << "numOriNodes: " << numOriNodes << endl;

    adjExternal.resize(numOriNodes);
    cout << "adjExternal size: " << adjExternal.size() << endl;
    cout << "supernodenum: " << supernodenum << endl;

    loadIdMapping((output_prefix + "id_mapping.txt").c_str());

    globalDegree.resize(numOriNodes);
    loadExternalAdjHalf((output_prefix + "external_adj.txt").c_str());

    loadBitCodesBinary(gtbin_path.c_str());

    // gtMask[u] = common-bit mask of GT codes over all (half) neighbors of u; the smallest triangle vertex has mask 0 (lossless prune)
    computeGtMask();

    size_t gt_memory_footprint = gtCode.size() * sizeof(BitCode);

    // Precondition: neighbors in each external_adj row sorted ascending (guaranteed at generation), giving A < B < C enumeration
    // repetitions for timing (default 5); the count itself is deterministic
    int k = (argc >= 3) ? atoi(argv[2]) : 5;
    std::chrono::duration<double> duration{0};
    for (int i = 0; i < k; i++) { 
            resetTriStats();
            auto start = std::chrono::steady_clock::now();
            tricount();
            auto end = std::chrono::steady_clock::now();
            duration += (end - start);
    }

    cout << "--------------------------------------------------------------- " << "\n";
    cout << "Time taken: " << duration.count()/k << " seconds\n";
    printTriStats();

    return 0;
}
