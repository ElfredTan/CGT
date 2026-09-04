#pragma GCC optimize(3,"Ofast","inline")
#include "triac_gt.h"
#include "graph.h"
#include <algorithm>
#include <chrono>
#include <iostream>

using namespace std;

// ========== 三角形计数统计变量 ==========

long long triangleCount = 0;
long long no_edge_and_gt_get = 0;
long long no_edge_but_gt_noget = 0;
long long have_edge_and_gt_noget = 0;


// ========== 三角形计数函数实现 ==========

void resetTriStats() {
    triangleCount = 0;
    no_edge_and_gt_get = 0;
    no_edge_but_gt_noget = 0;
    have_edge_and_gt_noget = 0;
}

void printTriStats() {
    cout << "--------------------------------------------------------------- " << "\n";
    cout << "triangleCount: " << triangleCount << "\n";
    cout << "no_edge_and_gt_get: " << no_edge_and_gt_get << "\n";
    cout << "no_edge_but_gt_noget: " << no_edge_but_gt_noget << "\n";
    cout << "have_edge_and_gt_noget: " << have_edge_and_gt_noget << "\n";
    cout << "this is : gt (A-prune + neighbor-GT-prune, no mask-preprune; NO diag counters)" << "\n";
    cout << "---------------------------------------------------- " << "\n";
}

void tricount() {
    // 主算法循环
    for (int i = 0; i < numOriNodes; i++) {
        // A
        int actualpointA = i;

        int asize = static_cast<int>(adjExternal[actualpointA].size());
        if (!asize) continue;

        // mask剪枝(A): 若 A 的所有邻居 GT 存在公共位(gtMask[A]!=0),
        // 说明 A 的所有大邻居属于同一独立集 -> 邻居间两两无边 -> A 不可能在任何三角形中, 直接跳过。
        // 无损: 三角形{x,y,z}(x<y<z)的最小顶点x必满足 gtMask[x]==0。
        if (gtMask[actualpointA].any()) {
            continue;
        }

        auto itlegalbeginA = adjExternal[actualpointA].begin();
        auto itlegalendA = adjExternal[actualpointA].end();

        // B
        for (int j = 0; j < asize; j++) {
            int actualpointB = adjExternal[i][j];

            int bsize = static_cast<int>(adjExternal[actualpointB].size());
            if (!bsize) continue;

            // 两个邻接表不可能相交先排除
            if (adjExternal[actualpointA][asize - 1] < adjExternal[actualpointB][0] ||
                adjExternal[actualpointA][0] > adjExternal[actualpointB][bsize - 1])
                continue;

            auto itlegalbeginB = adjExternal[actualpointB].begin();
            auto itlegalendB = adjExternal[actualpointB].end();

            if (asize > bsize) {
                BitCode GTpointA = gtCode[actualpointA];

                for (auto itB = itlegalbeginB; itB != itlegalendB; ++itB) {
                    int neighborB = *itB;

                    // GT剪枝操作
                    if ((gtCode[neighborB] & GTpointA).any()) {
                        // no_edge_and_gt_get++;
                        continue;
                    }

                    if (std::binary_search(itlegalbeginA, itlegalendA, neighborB)) {
                        triangleCount++;
                        // have_edge_and_gt_noget++;
                    }
                    // else {
                    //     no_edge_but_gt_noget++;
                    // }
                }
            }
            else {
                BitCode GTpointB = gtCode[actualpointB];

                for (auto itA = itlegalbeginA; itA != itlegalendA; ++itA) {
                    int neighborA = *itA;

                    // // GT剪枝操作
                    if ((gtCode[neighborA] & GTpointB).any()) {
                        // no_edge_and_gt_get++;
                        continue;
                    }

                    if (std::binary_search(itlegalbeginB, itlegalendB, neighborA)) {
                        triangleCount++;
                        // have_edge_and_gt_noget++;
                    }
                    // else {
                    //     no_edge_but_gt_noget++;
                    // }
                }
            }
        } // end j
    } // end i
}

// ========== 主函数 ==========

int main(int argc, char** argv) {
    // 参数检查
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <dataset_name>" << endl;
        return 1;
    }

    // 获取数据集名称
    string dataset_name = argv[1];

    // 构建基于数据集名称的路径前缀
    string output_prefix = "../data/output_" + dataset_name + "/";
    string gtbin_path = "../data/gtbin/" + dataset_name + ".bin";

    cout << "Dataset: " << dataset_name << endl;

    // 通过数据集参数加载
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

    // GT 编码
    loadBitCodesBinary(gtbin_path.c_str());

    // mask计算函数，对于当前节点，计算出后一半邻接表的GT&作为mask
    computeGtMask();

    size_t gt_memory_footprint = gtCode.size() * sizeof(BitCode);
    cout << "GT数据总大小: " << gt_memory_footprint / (1024 * 1024) << " MB\n";

    // 有序，保证 超级节点A < B < C
    // 前提是每一行的邻居已经升序排序，在生成图里面去做

    int k=1;
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
