#pragma GCC optimize(3,"Ofast","inline")
#include "trioffline.h"
#include "graph.h"
#include <algorithm>
#include <chrono>
#include <iostream>
#include <fstream>
#include <sstream>

using namespace std;

// ========== 三角形计数函数实现 ==========

// 复合查边 (与 matching has_edge_in_data / cdac isNeighbor 统一)
// 内部边(同超节点): 按形状判定 — clique 全真 / star·diamond 摘要槽位 / path 用 path_pos O(1)
// 外部边: GT 非边排除 (GT[u]&GT[v] 有公共位 ⟹ 非边) + adjExternal 二分确认 (adjExternal 为全边, 升序)
bool hasEdge(int u, int v) {
    if (u == v) return false;
    if (nodeToSuper[u] == nodeToSuper[v]) {
        int sid = nodeToSuper[u];
        if (sid >= firstSingleId) return false;   // single 单成员, 无内部边
        const SuperNode &S = superNodes[sid];
        if (S.type == 1) return true;         // clique: 所有内部对均有边
        if (S.type == 2)                      // star: 中心-叶子有边
            return u == S.beginnode || v == S.beginnode;
        if (S.type == 3)                      // diamond: begin/end 与任意成员有边
            return u == S.beginnode || u == S.endnode ||
                   v == S.beginnode || v == S.endnode;
        if (S.type == 4)                      // path: |Δpos|==1 (path_pos O(1))
            return g_path_pos_ok && queryPathPos((uint32_t)u, (uint32_t)v);
        return false;
    }
    // 外部边: GT 剪枝 + adjExternal 二分
    if ((gtCode[u] & gtCode[v]).any()) return false;
    if (u < v) return binary_search(adjExternal[u].begin(), adjExternal[u].end(), v);
    return binary_search(adjExternal[v].begin(), adjExternal[v].end(), u);
}

// 1. 内部三角形（三个节点都属于同一超节点）
long long countInternal(int sid, const vector<SuperNode> &superNodes) {
    if (sid >= firstSingleId) return 0;  // single 单成员, 无内部三角形
    const SuperNode &S = superNodes[sid];
    if (S.type == 1) {  // clique
        long long n = static_cast<long long>(S.size);
        return n * (n - 1) * (n - 2) / 6; // C(n,3)
    }
    if (S.type == 3) {  // diamond
        return 2;
    }
    // 目前形状认为只有团和钻石有全内部三角形
    return 0;
}

// 2. 一内二外（一个节点在超节点内，另两个节点在不同超节点）
long long countOneInTwoOut(int sid, const vector<SuperNode> &superNodes) {
    long long res = 0;

    for (int u : superNodeMembers[sid]) {
        // 遍历邻居超节点（使用superGraph2）
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

// 3. 二内一外（两个节点在超节点内，一个节点在外）
long long countTwoInOneOut(int sid, const vector<SuperNode> &superNodes) {
    long long res = 0;

    if (sid >= firstSingleId) return 0;  // single: 单成员, 无"二内"
    const SuperNode &S = superNodes[sid];
    if (S.type == 4 || S.type == 1) return 0;  // path(4)/clique(1) 不满足2内1外
    // 这几个类型不满足2内1外要求

    // 二内必须是星，星必须是含有一个中心
    for (size_t i = 0; i < superNodeMembers[sid].size(); i++) {
        int u = superNodeMembers[sid][i];

        for (size_t j = i + 1; j < superNodeMembers[sid].size(); j++) {
            int v = superNodeMembers[sid][j];

            // 判断内部是否有边 (统一走复合查边 hasEdge, star/diamond 分支语义不变)
            if (!hasEdge(u, v)) continue;

            // 计算mask
            BitCode mask = (gtCode[u] | gtCode[v]);

            // 遍历外部邻居
            for (int nid : superAdj[sid]) {
                for (int w : superNodeMembers[nid]) {
                    BitCode codeUnion = mask & gtCode[w];

                    if (codeUnion.none()) {
                        if (hasEdge(u, w) && hasEdge(v, w)) {
                            res++;
                        }
                    }
                }
            }
        }
    }

    return res;
}

// 主计数函数：查非三外三角形（内部+一内二外+二内一外）
void countTriangles0(const vector<SuperNode> &superNodes, TriResult0 &result) {
    result.internal = 0;
    result.oneInTwoOut = 0;
    result.twoInOneOut = 0;

    for (int sid = 0; sid < (int)superNodes.size(); ++sid) {
        // 1. 内部三角形
        result.internal += countInternal(sid, superNodes);

        // 2. 一内二外
        result.oneInTwoOut += countOneInTwoOut(sid, superNodes);

        // 3. 二内一外
        result.twoInOneOut += countTwoInOneOut(sid, superNodes);
    }
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

    // 读取摘要统计信息
    readSummaryStats((output_prefix + "summary.txt").c_str());
    cout << "From summary.txt: numRemainNodes: " << numRemainNodes
         << ", supernodenum: " << supernodenum
         << ", numOriNodes: " << numOriNodes << endl;
    cout << "numRemainNodes: " << numRemainNodes << endl;
    cout << "numOriNodes: " << numOriNodes << endl;

    // 调整大小
    adjExternal.resize(numOriNodes);
    // 统一版 loadExternalAdj 会填 globalDegree (subac 语义), tri 不用它但必须先分配
    globalDegree.resize(numOriNodes);
    cout << "adjExternal size: " << adjExternal.size() << endl;
    cout << "supernodenum: " << supernodenum << endl;

    // 加载ID映射
    loadIdMapping((output_prefix + "id_mapping.txt").c_str());

    // 加载邻接表（使用 loadExternalAdj）
    loadExternalAdj((output_prefix + "external_adj.txt").c_str());

    // 加载GT二进制文件
    loadBitCodesBinary(gtbin_path.c_str());

    // 加载映射信息
    loadMappingTxt((output_prefix + "mapping.txt").c_str());
    loadMembersTxt((output_prefix + "members.txt").c_str());   // fC' 填 superNodeMembers(保序, 含single)

    // path 内部 O(1) 查边结构 (hasEdge 的 path 分支依赖)
    loadPathPos(output_prefix + "path_pos.txt");

    // 加载超图邻接关系
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

    // 加载超级节点摘要
    superNodes = loadSuperGraph((output_prefix + "synopsis.txt").c_str());

    size_t gt_memory_footprint = gtCode.size() * sizeof(BitCode);
    cout << "GT数据总大小: " << gt_memory_footprint / (1024 * 1024) << " MB\n";

    // 开始计时
    auto start = std::chrono::steady_clock::now();

    // 执行三角形计数
    TriResult0 result;
    countTriangles0(superNodes, result);

    auto end = std::chrono::steady_clock::now();
    std::chrono::duration<double> duration = end - start;

    cout << "--------------------------------------------------------------- " << "\n";
    cout << "Time taken: " << duration.count() << " seconds\n";

    // 打印统计结果
    cout << "--------------------------------------------------------------- " << "\n";
    cout << "非三外三角形计数结果:" << "\n";
    cout << "内部三角形 (internal): " << result.internal << "\n";
    cout << "一内二外 (oneInTwoOut): " << result.oneInTwoOut << "\n";
    cout << "二内一外 (twoInOneOut): " << result.twoInOneOut << "\n";
    cout << "总计 (total): " << result.total() << "\n";
    cout << "---------------------------------------------------- " << "\n";

    return 0;
}
