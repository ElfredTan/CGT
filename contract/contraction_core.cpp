// Graph.cpp
#pragma GCC optimize(3,"Ofast","inline")
#include "Graph.h"
#include <map>
#include <fstream>
#include <sstream>
#include <queue>
#include <iostream>
#include <functional>
#include <iostream>
#include <string>  
#include <sys/stat.h>
#include <algorithm>
#include <set>
#include <chrono>

using namespace std;

inline int typeToCode(const std::string& t) {
    if (t == "clique")  return 1;
    if (t == "star")    return 2;
    if (t == "diamond") return 3;
    if (t == "path")    return 4;
    if (t == "single")  return 5;
    return 0;
}

std::map<std::string, int> typeCounts;
std::map<std::string, int> typeNodeCounts; // 统计每种类型的节点总数
string datasetname = "unknown";

int global_contracted_no_single_node_count = 0;
int remainingNodes = 0;
int supernode_id = 0;


// 全局紧凑，超级节点连续编号
long long nodenum=-1, edgenum=-1;
long long remainingEdges = -1;
void Graph::loadEdgeFormat(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("Cannot open file: " + filename);

    // 读取第一行：节点数和边数
    std::string first_line;
    if (!getline(file, first_line)) {
        throw std::runtime_error("File is empty: " + filename);
    }
    
    std::istringstream iss(first_line);
    
    if (!(iss >> nodenum >> edgenum)) {
        throw std::runtime_error("First line must contain node count and edge count");
    }
    std::cout<<"nodenum:" << nodenum<<" "<<"edgenum:"<<edgenum<<endl;
    
    nodes.resize(nodenum);
    supernodes_members.resize(nodenum);
    synopsis_list.resize(nodenum);

    // 第二次扫描：填充边数据
    std::string line;  // ← 添加这一行声明 line 变量
    int edge_count = 0;
    while (getline(file, line)) {
        std::istringstream line_iss(line);  // 改名避免与前面的 iss 冲突
        int src, dst;

        // 使用空白字符（空格/Tab）作为分隔符读取src和dst
        if (!(line_iss >> src >> dst)) {
            std::cerr << "Warning: Invalid line format: " << line << std::endl;
            continue;
        }
        
        if (src == dst) continue;
        
        if (src < 0 || src >= nodenum || dst < 0 || dst >= nodenum) {
            std::cerr << "Warning: Node ID out of range: " << src << " " << dst << std::endl;
            continue;
        }

        // 添加边（无向图双向添加）
        nodes[src].neighbors.push_back(dst);
        nodes[dst].neighbors.push_back(src);
        nodes[src].id = src;
        nodes[dst].id = dst;
        edge_count++;
    }

    // ========== 排序所有邻居列表 ==========
    for (auto& node : nodes) {
        if (!node.neighbors.empty()) {
            std::sort(node.neighbors.begin(), node.neighbors.end());
            // 可选：去重（如果输入可能有重复边）
        }
    }
    
    // 可选：输出读取统计
    std::cout << "Loaded " << nodenum << " nodes and " << edge_count << " edges from " << filename << std::endl;
}


// 构建扁平化摘要 start_node can be startcenter or pathbegin
void Graph::buildSynopsisFlat(int super_node_id, const std::string& type,
                                const std::vector<int>& members, int start_node) {

    Synopsis& syn = synopsis_list[super_node_id];
    syn.type = static_cast<uint8_t>(typeToCode(type));   // 内存即 uint8 编码(id 已删, 下标即id)
    syn.size = members.size();
    syn.center_or_pathbegin = start_node;
    syn.pathend = -1;

    // 单次遍历计算摘要
    int max_deg = 0;
    for (int member : members) {
        const auto& node = nodes[member];
        max_deg = std::max(max_deg, static_cast<int>(node.neighbors.size()));
    }
    syn.max_degree = max_deg;

    // 类型特定计算node1  node2
    if (type == "path") {
        syn.center_or_pathbegin = members.front();
        syn.pathend = members.back();
    } else if (type == "diamond") {   // 3: pathend=基础边 v 端(begin 已是 u 端)
        // diamond members 顺序 [u,v,x,y]: u=start(已存 center_or_pathbegin), v=基础边另一端
        syn.pathend = members[1];
    }
}

// contractTopologicalComponents: 按 typeOrder 给定的顺序收缩拓扑组件
void Graph::contractTopologicalComponents(int kl, int ku, const std::vector<std::string>& typeOrder) {
    // 按 typeOrder 顺序依次收缩, typeOrder 由调用方决定形状种类与先后
    for (const std::string& type : typeOrder) {
        contractSpecificType(kl, ku, type, supernode_id);
    }
}

static inline void intersectInPlace(std::vector<int>& common, const std::vector<int>& sortedNbrs) {
    size_t i = 0, j = 0, w = 0;
    while (i < common.size() && j < sortedNbrs.size()) {
        if (common[i] < sortedNbrs[j]) ++i;
        else if (common[i] > sortedNbrs[j]) ++j;
        else { common[w++] = common[i]; ++i; ++j; }
    }
    common.resize(w);
}

bool Graph::growCliqueFrom(int start, std::vector<bool>& localVisited, std::vector<int>& group,int kl,int ku) {
    // 初始检查（不变）
    if (start < 0 || start >= localVisited.size() ||
        (start < fC.size() && fC[start] >= 0) || localVisited[start] ||
        start >= nodes.size()) return false;
    
    
    group.clear();
    group.push_back(start);
    std::vector<int> candidates;

    // 收集候选邻居（排除已收缩/访问的节点）
    for (int nb : nodes[start].neighbors) {
        if (nb >= 0 && nb < localVisited.size() &&
            !(nb < fC.size() && fC[nb] >= 0) && !localVisited[nb] &&
            nb < nodes.size()) {
            candidates.push_back(nb);
        }

    }

    // 下界初步检查，kl=4,邻居需要至少3个，及时剪枝
    if(candidates.size() < kl-1) return false;
    

    // 按度数降序排序（优先尝试高度数节点）
    std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
        return m_degree[a] > m_degree[b];
    });

    std::vector<int> common_sorted = candidates;
    std::sort(common_sorted.begin(), common_sorted.end());   // id 升序

    // 逐步构建最大可能的团
    for (int cand : candidates) {
        // 度数检查
        if (m_degree[cand] < group.size()) break;

        // cand 连当前 group 全员? ⟺ cand ∈ common_sorted
        bool isClique = std::binary_search(common_sorted.begin(), common_sorted.end(), cand);
        if (isClique) {
            group.push_back(cand);
            // 适可而止 到了上界就该跳出来
            if(group.size()==ku)
            {
                break;
            }
            // push 后更新 common = common ∩ nodes[cand].neighbors(两已排序 vector 求交)
            intersectInPlace(common_sorted, nodes[cand].neighbors);
        }
    }

    // 至少需要4个节点（包括start）
    if (group.size() < kl) return false;

    // 标记访问
    for (int v : group) localVisited[v] = true;
    return true;
}


// 注意不能作为中心的节点仍可能成为叶子，先局部排除，不能先全局排除visit
bool Graph::growStarFrom(int center, std::vector<bool>& localVisited, std::vector<int>& group,int kl,int ku) {
    // 边界检查（保持不变）
    if (center < 0 || center >= localVisited.size() ||
        (center < fC.size() && fC[center] >= 0) || localVisited[center] ||
        center >= nodes.size()) return false;

    // 第一阶段：收集候选叶子，保持遍历顺序
    std::vector<int> potential_leaves;
    for (int nb : nodes[center].neighbors) {
        if (!(nb < fC.size() && fC[nb] >= 0) && !localVisited[nb] && nb < nodes.size()) {
            potential_leaves.push_back(nb);
        }
        // 上限
        if(potential_leaves.size()==ku-1)
        {
            break;
        }
    }

    // 下限
    if (potential_leaves.size() < static_cast<size_t>(kl - 1)) {
        return false;
    }

    // 按度数增序排序（优先选择度数较小的叶子）
    std::sort(potential_leaves.begin(), potential_leaves.end(), [this](int a, int b) {
        return m_degree[a] < m_degree[b];
    });

    // 第二阶段：依次尝试加入叶子，只与已加入的叶子检查无连接
    std::vector<int> selected_leaves;

    for (int leaf : potential_leaves) {
        // 第一个叶子直接加入，无需检查
        if (selected_leaves.empty()) {
            selected_leaves.push_back(leaf);
            continue;
        }

        // 检查当前叶子与所有已选叶子是否无连接
        bool can_add = true;
        for (int selected : selected_leaves) {
            // optStar: 邻居表无向双向一致(loadEdgeFormat 双向加边), 单次查等价, 删冗余第二次 binary_search
            if (std::binary_search(nodes[leaf].neighbors.begin(), nodes[leaf].neighbors.end(), selected)) {
                can_add = false;
                break;
            }
        }

        // 如果与所有已选叶子都无连接，则加入
        if (can_add) {
            selected_leaves.push_back(leaf);

            // 达到上限就停止
            if (selected_leaves.size() >= static_cast<size_t>(ku - 1)) {
                break;
            }
        }
        // 否则跳过当前叶子，继续尝试下一个
    }

    // 第三阶段：检查数量是否满足阈值
    if (selected_leaves.size() >= static_cast<size_t>(kl - 1)) {
        // 构建最终的 group
        group.push_back(center);
        group.insert(group.end(), selected_leaves.begin(), selected_leaves.end());

        // 标记所有节点为已访问
        for (int v : group) {
            if (v >= 0 && v < localVisited.size()) {
                localVisited[v] = true;
            }
        }

        return true;
    }

    return false;
}


bool Graph::growPathFrom(
    int start,
    std::vector<bool>& localVisited,
    std::vector<int>& group,
    int kl,
    int ku)
{
    if (start < 0 ||
        start >= localVisited.size() ||
        (start < fC.size() && fC[start] >= 0) ||
        localVisited[start] ||
        start >= nodes.size())
    {
        return false;
    }

    // start 必须是严格二度点
    if (m_degree[start] != 2)
        return false;

    // 找两个合法邻居
    int nearneighbor[2];
    int idx = 0;

    for (int nb : nodes[start].neighbors)
    {
        if ((nb < fC.size() && fC[nb] >= 0) ||
            localVisited[nb] ||
            nb >= nodes.size())
        {
            return false;
        }

        nearneighbor[idx++] = nb;
    }

    // 路径左右部分
    std::vector<int> leftChain;
    std::vector<int> rightChain;

    // membership cache
    std::unordered_set<int> groupSet;
    groupSet.insert(start);

    auto extendDirection =
        [&](int curr,
            int prev,
            std::vector<int>& chain)
    {
        while (true)
        {
            // 成环检测
            if (groupSet.count(curr))
            {
                return;
            }

            // 当前节点加入路径
            chain.push_back(curr);
            groupSet.insert(curr);

            // 长度上限
            size_t totalSize =
                leftChain.size() +
                rightChain.size() +
                1;

            if (totalSize >= static_cast<size_t>(ku))
            {
                return;
            }

            // 内部节点条件：
            // 必须全局度=2
            if (m_degree[curr] != 2)
            {
                return;
            }

            // 找 nextNode
            int nextNode = -1;

            for (int nb : nodes[curr].neighbors)
            {
                if (nb == prev)
                    continue;

                if ((nb < fC.size() && fC[nb] >= 0) ||
                    localVisited[nb] ||
                    nb >= nodes.size())
                {
                    continue;
                }

                nextNode = nb;
                break;
            }

            // 没法继续
            if (nextNode == -1)
            {
                return;
            }

            prev = curr;
            curr = nextNode;
        }
    };

    // 向左
    extendDirection(
        nearneighbor[0],
        start,
        leftChain);

    // 向右
    extendDirection(
        nearneighbor[1],
        start,
        rightChain);

    // merge
    group.clear();

    std::reverse(
        leftChain.begin(),
        leftChain.end());

    group.insert(
        group.end(),
        leftChain.begin(),
        leftChain.end());

    group.push_back(start);

    group.insert(
        group.end(),
        rightChain.begin(),
        rightChain.end());

        // 最后检查两个端点是否连接，删除度数更大的？
        int leftEnd = group.front();
        int rightEnd = group.back();

        if (std::binary_search(
                nodes[leftEnd].neighbors.begin(),
                nodes[leftEnd].neighbors.end(),
                rightEnd))
        {
            // trim one endpoint
            if (m_degree[leftEnd] >
                m_degree[rightEnd])
            {
                group.erase(group.begin());
            }
            else
            {
                group.pop_back();
            }
        }

            // 长度检查
    if (group.size() < static_cast<size_t>(kl))
    {
        return false;
    }

    // 更新 visited
    for (int v : group)
    {
        localVisited[v] = 1;
    }

    return true;
}

// diamond：先选边 (u,v)，再选 x、y 满足 (a) 都连 u 和 v，(b) x、y 之间不连。
// start 作为 u，遍历 start 的合法邻居作为 v，固定 4 节点结构。
bool Graph::growDiamondFrom(int start, std::vector<bool>& localVisited, std::vector<int>& group,int kl,int ku) {
    // 初始检查（与 clique 一致）
    if (start < 0 || start >= localVisited.size() ||
        (start < fC.size() && fC[start] >= 0) || localVisited[start] ||
        start >= nodes.size()) return false;

    // 收集 u=start 的合法邻居（作为候选 v），排除已收缩/访问
    std::vector<int> candidates;
    for (int nb : nodes[start].neighbors) {
        if (nb >= 0 && nb < localVisited.size() &&
            !(nb < fC.size() && fC[nb] >= 0) && !localVisited[nb] &&
            nb < nodes.size()) {
            candidates.push_back(nb);
        }
    }

    // diamond 至少 4 个节点，u 至少要有 1 个邻居 v，且共同邻居至少 2 个
    // 这里无法提前精确剪枝，逐个 v 尝试
    if (candidates.size() < 1) return false;

    // 按度数降序，优先高度数 v（共同邻居更多，更易凑成 diamond）
    std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
        return m_degree[a] > m_degree[b];
    });

    // 逐个 v 尝试：找两个共同邻居 x、y，且 x、y 不相连
    for (int v : candidates) {
        // 收集 u 和 v 的共同合法邻居（排除 u、v 自身，且未访问）
        std::vector<int> common;
        for (int nb : nodes[start].neighbors) {
            if (nb == v) continue;
            if (nb < 0 || nb >= localVisited.size() ||
                (nb < fC.size() && fC[nb] >= 0) || localVisited[nb] ||
                nb >= nodes.size()) {
                continue;
            }
            // nb 也是 v 的邻居 → 共同邻居
            if (std::binary_search(nodes[v].neighbors.begin(), nodes[v].neighbors.end(), nb)) {
                common.push_back(nb);
            }
        }

        // 共同邻居不足 2 个，凑不出 x、y
        if (common.size() < 2) continue;

        // 在共同邻居中找一对不相连的 (x, y)
        bool found = false;
        int x = -1, y = -1;
        for (size_t i = 0; i < common.size() && !found; ++i) {
            for (size_t j = i + 1; j < common.size() && !found; ++j) {
                int a = common[i];
                int b = common[j];
                // (a) x、y 之间不连（双向都查，邻居表已排序）
                // optStar: 邻居表无向双向一致, 单次查等价, 删冗余第二次 binary_search
                bool connected = std::binary_search(nodes[a].neighbors.begin(), nodes[a].neighbors.end(), b);
                if (!connected) {
                    x = a;
                    y = b;
                    found = true;
                }
            }
        }

        if (!found) continue;

        // 构建 group：u=start, v, x, y（4 节点）
        // 注意 start 此时未必在 group 里，先 clear 再 push
        group.clear();
        group.push_back(start);
        group.push_back(v);
        group.push_back(x);
        group.push_back(y);

        // 固定 4 节点，kl/ku 对 diamond 仅作下界参考（要求 kl<=4）
        if (static_cast<int>(group.size()) < kl) return false;

        // 标记访问
        for (int vtx : group) localVisited[vtx] = true;
        return true;
    }

    return false;
}

void Graph::contractSpecificType(int kl, int ku, const std::string& type, int& supernode_id) {
    size_t max_node_id = nodes.size() - 1;

    // 确保vector大小足够
    // 相当于每次调用形状 local会更新 全局是共用的
    std::vector<bool> localVisited(max_node_id + 1, false);

    // ===================== 插入：排序逻辑 =====================
    std::vector<int> sortedIds;
    for (size_t id = 0; id < nodes.size(); ++id) {
        if (id >= localVisited.size() ||
            (id < fC.size() && fC[id] >= 0) || localVisited[id]) {
            continue;
        }
        sortedIds.push_back(id);
    }

    std::sort(sortedIds.begin(), sortedIds.end(), [&](int a, int b) {
        return m_degree[a] > m_degree[b]; // 度数大的优先
    });
    // ========================================================

    // 遍历排序后的节点（替换原来的 for (const auto& [id, node] : nodes)）
    for (int id : sortedIds) {
        // id就是生长函数的start
        if (id < 0 || id >= localVisited.size() ||
            (id < fC.size() && fC[id] >= 0) || localVisited[id]) {
            continue;
        }

        std::vector<int> group;
        bool success = false;

        // 计时: grow 搜索(按形状细分 + 总计)
        if (type == "clique") {
            success = growCliqueFrom(id, localVisited, group,kl, ku);
        } else if (type == "diamond") {
            success = growDiamondFrom(id, localVisited, group,kl, ku);
        } else if (type == "star") {
            success = growStarFrom(id, localVisited, group,kl, ku);
        } else if (type == "path") {
            success = growPathFrom(id, localVisited, group,kl, ku);
        }

        if (success && group.size() >= kl && group.size() <= ku) {
            // simp2: globallyContracted 写入已删(≡fC[v]>=0, 由下方 fC[v]=sn.id 接管)
            // 创建超级节点
            int current_supernode_id = supernode_id++;

            // 更新 fC 映射
            for (int v : group) {
                fC[v] = current_supernode_id;
            }

            // 存储成员
            supernodes_members[current_supernode_id] = group;

            //计数器更新
            typeCounts[type]++;
            typeNodeCounts[type] += group.size();
            // 构建并存储摘要
            buildSynopsisFlat(current_supernode_id, type, group, id);


        }
    }
}


// exportContractedGraph: 修改摘要导出逻辑
void Graph::exportContractedGraph(const std::string& basepath,const std::string& dataset_name) {
    std::cout << "Exporting FULL contracted graph to: " << basepath << std::endl;
    
    // 1. 创建目录
    struct stat info;
    if (stat(basepath.c_str(), &info) != 0) {
        std::string cmd = "mkdir -p " + basepath;
        if (system(cmd.c_str()) != 0) {
            std::cerr << "Error: Failed to create directory " << basepath << std::endl;
            return;
        }
    }

    // 2. 清空并准备容器
    contractedEdges.clear(); 
    // ========== 遍历构建超边信息 ==========

    for (size_t srcId = 0; srcId < nodes.size(); ++srcId) {
        // 构建超边信息（原有逻辑）
        if (srcId >= fC.size() || fC[srcId] < 0) continue;

        int superSrc = fC[srcId];
        for (int dstId : nodes[srcId].neighbors) {
            if (dstId >= fC.size() || fC[dstId] < 0) continue;
            if (srcId > static_cast<size_t>(dstId)) continue; // 避免重复

            int superDst = fC[dstId];
            if (superSrc != superDst) {
                contractedEdges[superSrc].insert(superDst);   // 超图邻接表(保留)
                contractedEdges[superDst].insert(superSrc);
            }
        }
    }

    // 3. 输出超级图（super_adj.txt）
    std::ofstream gfile(basepath + "/super_adj.txt");
    if (!gfile.is_open()) throw std::runtime_error("Cannot open super_adj.txt");

    gfile << "SuperNodeID Edges\n";
    for (int id = 0; id < supernode_id; id++) {   // simp1: supernodes 遍历换 supernode_id 计数
        gfile << id << " ";
        if (contractedEdges.count(id)) {
            std::vector<int> neighbors(
                contractedEdges.at(id).begin(),
                contractedEdges.at(id).end()
            );
            std::sort(neighbors.begin(), neighbors.end());
            for (size_t i = 0; i < neighbors.size(); i++) {
                if (i > 0) gfile << " ";
                gfile << neighbors[i];
            }
        }
        gfile << "\n";
    }
    gfile.close();

    int edgeCount = 0;
    for (const auto& [src, neighbors] : contractedEdges) {
        for (int dst : neighbors) {
            if (src < dst) continue;
            edgeCount++;
        }
    }

    // 6. 输出 mapping.txt
    //    下游 loadMappingTxt 只填 nodeToSuper(按 origNode 索引), 不依赖同 sid 内顺序, 故统一按 members 顺序
    //    path/diamond 成员顺序天然保序(group 顺序)
    std::ofstream mappingFile(basepath + "/mapping.txt");
    if (!mappingFile.is_open()) throw std::runtime_error("Cannot open mapping.txt");

    mappingFile << "OriginalNodeID\tSuperNodeID\tSuperNodeType\n";
    for (int sid = 0; sid < supernode_id; ++sid) {
        if (sid >= static_cast<int>(supernodes_members.size()) || supernodes_members[sid].empty()) continue;
        // type: non-single 从 synopsis_list 取; single(sid>=singlemin 或 synopsis_list 无效) hardcode 5
        int typecode;
        if (sid < static_cast<int>(synopsis_list.size()) && synopsis_list[sid].type != 0) {
            typecode = synopsis_list[sid].type;   // 已是 uint8 编码; 有效标记=type!=0
        } else {
            typecode = 5;  // single 或 越界槽
        }
        for (int nid : supernodes_members[sid]) {
            mappingFile << nid << "\t" << sid << "\t" << typecode << "\n";
        }
    }
    mappingFile.close();


    // 7. 输出 synopsis.txt（保持不变）
    std::ofstream synopsisFile(basepath + "/synopsis.txt");
    if (!synopsisFile.is_open()) throw std::runtime_error("Cannot open synopsis.txt");

    // synopsis 新格式 - 无圆括号type, 无Members, 不含single, type=数字
    for (size_t id = 0; id < synopsis_list.size(); ++id) {
        if (id >= supernodes_members.size() || supernodes_members[id].empty()) {
            continue;
        }
        const auto& syn = synopsis_list[id];
        if (syn.type == 0) continue;   // simp3→0815: 空槽/未填(type=0)跳过(id 已删, 用 type 判有效)

        synopsisFile << "SuperNode " << id << " Synopsis{";
        synopsisFile << "type=" << static_cast<int>(syn.type);   // 已是 uint8 编码
        synopsisFile << ",size=" << syn.size;
        synopsisFile << ",md=" << syn.max_degree;

        // begin/end = 形状的两个特殊节点(通用槽位, type 区分语义):
        //   star(2):    begin=center, 无 end      (center 不另设 c=, 统一放 begin)
        //   path(4):    begin/end=链两端
        //   diamond(3): begin/end=基础边(u,v)两端
        //   clique(1):  无特殊节点, 不写
        if (syn.type == 2) {            // star: 只写 begin(=center)
            synopsisFile << ",begin=" << syn.center_or_pathbegin;
        } else if (syn.type == 4 || syn.type == 3) {   // path/diamond: 写两端
            synopsisFile << ",begin=" << syn.center_or_pathbegin;
            synopsisFile << ",end=" << syn.pathend;
        }
        synopsisFile << "}\n";
    }
    synopsisFile.close();

    // 7b. 输出 members.txt (fC': 超节点 -> 成员数组, 仿 super_adj.txt 格式)
    //     含 single; path/diamond 直接遍历 supernodes_members[id] 天然保序
    std::ofstream membersFile(basepath + "/members.txt");
    if (!membersFile.is_open()) throw std::runtime_error("Cannot open members.txt");
    membersFile << "# SuperNodeID member1 member2 ...\n";
    // 上界改用 supernodes_members.size() —— synopsis_list 已缩到非single, 不能再当 members 的界
    for (size_t id = 0; id < supernodes_members.size(); ++id) {
        if (supernodes_members[id].empty()) continue;
        membersFile << id;
        for (int m : supernodes_members[id]) membersFile << " " << m;
        membersFile << "\n";
    }
    membersFile.close();

    // path 内部位置映射导出 (供子算法 O(1) path 内部查边)
    exportPathPos(basepath);

    // 8. 输出 summary.txt（保持不变）
    std::ofstream summaryFile(basepath + "/summary.txt");
    if (!summaryFile.is_open()) throw std::runtime_error("Cannot open summary.txt");
    // new datasetname
    summaryFile << "Dataset_name: " << dataset_name << "\n";
    summaryFile << "Total_ori_nodes: " << nodes.size() << "\n";
    summaryFile << "Total_ori_edges: " << edgenum << "\n";

    summaryFile << "Total_super_nodes: " << supernode_id << "\n";
    summaryFile << "Total_super_edges: " << edgeCount << "\n";   // sortedEdges 已随 super_graph 注释删除

    summaryFile << "Total_remain_nodes: " << remainingNodes << "\n";
    summaryFile << "Total_remain_edges: " << remainingEdges << "\n";  // 省略内部边后的实际边数（=gt_input_edges.txt 表头）


    summaryFile << "Contracted_nodes_excluding_single: " << global_contracted_no_single_node_count << "\n";
    summaryFile << "Contracted_nodes_percentage: " 
            << (global_contracted_no_single_node_count * 100.0 / nodes.size()) << "%\n";
    
    for (const auto& [type, count] : typeCounts) {
        summaryFile << "Supernodes_type_" << type << ": " << count << "\n";
    }
    for (const auto& [type, node_count] : typeNodeCounts) {
        summaryFile << "Supernodes_type_" << type << "_node_count: " << node_count << "\n";
    }
    summaryFile.close();

    std::cout << "Export completed. All supernode types (clique/diamond/star/path/single) included.\n";
}

// wrapRemainingAsSingletonSupernodes: 将剩余未收缩的节点作为单节点超级节点
void Graph::wrapRemainingAsSingletonSupernodes() {
    for (size_t nid = 0; nid < nodes.size(); ++nid) {
        if (nid >= fC.size() || fC[nid] < 0) {
            int current_id = supernode_id++;
            fC[nid] = current_id;
            // single 是半成品, 不进 synopsis_list (只填 fC/fC')
            //   synopsis_list/supernodes_members 已在 loadEdgeFormat resize 到 nodenum, 无需再 resize
            // 存储成员 (fC' 正向)
            supernodes_members[current_id] = {static_cast<int>(nid)};

            // typeCounts 改 hardcode, 不依赖 synopsis_list
            typeCounts["single"]++;
        }
    }
}

// contract: 主收缩函数，包含详细调试信息
void Graph::contract(long timestamp_threshold, int kl, int ku, const std::vector<std::string>& typeOrder) {
    // 1. 清空之前的收缩结果
    supernode_id = 0;
    contractedEdges.clear();

    // ========== 初始化 fC vector ==========
    fC.assign(nodes.size(), -1);  // 全部初始化为 -1（未收缩）

    // 预计算度数缓存(contract 阶段 nodes 只读, 4 种 shape 共用)
    m_degree.assign(nodes.size(), 0);
    for (size_t i = 0; i < nodes.size(); ++i) m_degree[i] = static_cast<int>(nodes[i].neighbors.size());

 // 3. 拓扑结构收缩调试（新增详细调试信息）
    std::cout << "\n[Phase 2] Topological contraction analysis:" << endl;
    std::cout << " - Current contracted nodes: " << fC.size() << "/" << nodes.size() << endl;
    std::cout << " - Scanning for structures (size " << kl << "-" << ku << ")..." << endl;
    std::cout << " - Shape order: ";
    for (size_t i = 0; i < typeOrder.size(); ++i) {
        if (i) std::cout << " -> ";
        std::cout << typeOrder[i];
    }
    std::cout << endl;

    int before_topology = supernode_id;
    contractTopologicalComponents(kl, ku, typeOrder);
    std::cout << " - Added " << (supernode_id - before_topology)
              << " new topological supernodes" << endl;

    // 4. 最终统计（增强版）
    std::cout << "\n[Result]";
    std::cout << "\n - Total nodes: " << nodes.size();
    // 计算实际收缩节点数（非 -1 的数量）
    int contracted_count = std::count_if(fC.begin(), fC.end(), [](int v) { return v >= 0; });
    std::cout << "\n - Contracted nodes: " << contracted_count << " ("
              << (contracted_count*100/nodes.size()) << "%)";
    global_contracted_no_single_node_count = contracted_count;
    std::cout << "\n - Total supernodes: " << supernode_id;
    
    if ((supernode_id == 0)) {
        std::cout << "\n ! Warning: No supernodes created. Check:";
        std::cout << "\n   1. Threshold (" << timestamp_threshold << ") vs node timestamps";
        std::cout << "\n   2. Size range [" << kl << "," << ku << "] appropriateness";
        std::cout << "\n   3. Graph connectivity and structure";
    }
    std::cout << endl;
    // 新增最终验证
    std::cout << "\n[Validation] Supernode details:\n";

    
    // 原有结构压缩结束后
    wrapRemainingAsSingletonSupernodes(); // 调用新阶段
    {
        long long singleCnt = 0;
        auto it = typeCounts.find("single");
        if (it != typeCounts.end()) singleCnt = it->second;
        // ① 摘要: 只留 非 single 槽(single 不存摘要)
        synopsis_list.resize(static_cast<size_t>(supernode_id - singleCnt));
        synopsis_list.shrink_to_fit();
        // ② members: 必须保留全部 super(含 single) —— members.txt 输出/下游 loadMembersTxt 依赖
        supernodes_members.resize(supernode_id);
        supernodes_members.shrink_to_fit();
    }
}


// 导出 path 内部位置映射 path_pos.txt (每行 "node pos", 仅 path 节点)
void Graph::exportPathPos(const std::string& basepath) {
    std::ofstream ppfile(basepath + "/path_pos.txt");
    if (!ppfile.is_open()) throw std::runtime_error("Cannot open path_pos.txt");
    long long cnt = 0, npaths = 0;
    for (size_t sid = 0; sid < supernodes_members.size(); ++sid) {
        const auto& ms = supernodes_members[sid];
        if (ms.empty()) continue;
        if (synopsis_list[sid].type != 4) continue;   // 4 = path (typeToCode)
        if (ms.size() < 2) continue;
        // 注: 少数 single 包装复用了残留 type=4 的 synopsis 槽 (bk 实测 273 个),
        //     以 members.size()<2 过滤, 导出数与 synopsis.txt 口径(17701)严格一致。
        ++npaths;
        for (size_t i = 0; i < ms.size(); ++i)
            ppfile << ms[i] << " " << i << "\n";
        cnt += (long long)ms.size();
    }
    ppfile.close();
    std::cout << "[INFO] path_pos.txt: " << npaths << " 个 path, "
              << cnt << " 个 path 节点已导出" << std::endl;
}

// buildContractedGraphWithoutInternalEdges: 构建不包含内部边的新图
// 独立测超图邻接表构建(contractedEdges, 验证O(E)), 不含 superEdgeToEdges(输出用,不测)
void Graph::buildSuperGraphAdjacency() {
    contractedEdges.clear();
    for (size_t srcId = 0; srcId < nodes.size(); ++srcId) {
        if (srcId >= fC.size() || fC[srcId] < 0) continue;
        int superSrc = fC[srcId];
        for (int dstId : nodes[srcId].neighbors) {
            if (dstId >= fC.size() || fC[dstId] < 0) continue;
            if (srcId > static_cast<size_t>(dstId)) continue;   // 避免重复(无向边只处理一次)
            int superDst = fC[dstId];
            if (superSrc != superDst) {
                contractedEdges[superSrc].insert(superDst);   // 加邻居
                contractedEdges[superDst].insert(superSrc);
            }
        }
    }
}

void Graph::buildContractedGraphWithoutInternalEdges(const std::string &basepath) {

    // 重构为两阶段 —— 阶段1 构建(计时), 阶段2 写出(不计时). 保持输出字节级一致.
    remainingNodes = 0;
    std::vector<std::pair<int, int>> externalEdges;          // 所有外部边
    std::unordered_set<int> nodesWithExternalEdges;          // 有外部边的节点
    std::vector<std::vector<int>> validPerNode(nodes.size());   // vector替代set(循环后sort+unique)
    std::vector<std::vector<int>> insidePerNode(nodes.size());  // set→vector, 每节点内部邻居

    // ===== 阶段1: 构建(计时, 不含写文件) =====
    for (size_t u = 0; u < nodes.size(); ++u) {
        std::vector<int> validNeighbors;  
        std::vector<int> insideNeighbors;  
        for (int v : nodes[u].neighbors) {
            if(fC[u] != fC[v]) {
                validNeighbors.push_back(v);
                externalEdges.emplace_back(u, v);
                nodesWithExternalEdges.insert(u);
                nodesWithExternalEdges.insert(v);
            } else {
                insideNeighbors.push_back(v);  
            }
        }
        std::sort(insideNeighbors.begin(), insideNeighbors.end());
        insideNeighbors.erase(std::unique(insideNeighbors.begin(), insideNeighbors.end()), insideNeighbors.end());
        // adjoutside同款排序去重
        std::sort(validNeighbors.begin(), validNeighbors.end());
        validNeighbors.erase(std::unique(validNeighbors.begin(), validNeighbors.end()), validNeighbors.end());
        if(nodes[u].neighbors.empty()) {
            remainingNodes++;   // 仅 stdout 打印用, summary 的 Total_remain_nodes 由 newgraph 表头节点数覆盖
        }
        insidePerNode[u] = std::move(insideNeighbors);
        validPerNode[u] = std::move(validNeighbors);
    }

    // ===== 阶段2: 写出(不计时) =====
    std::string outputPath1 = basepath + "/external_adj.txt";
    std::string outputPath2 = basepath + "/internal_adj.txt";   // 恢复
    std::ofstream outFile(outputPath1);
    if (!outFile.is_open()) throw std::runtime_error("Cannot open output file: " + outputPath1);
    std::ofstream outFile3(outputPath2);   // 恢复
    if (!outFile3.is_open()) throw std::runtime_error("Cannot open output file: " + outputPath2);

    for (size_t u = 0; u < nodes.size(); ++u) {
        const auto& validNeighbors = validPerNode[u];
        const auto& insideNeighbors = insidePerNode[u];
        if (!validNeighbors.empty()) {
            outFile << u << ":";
            for (int v : validNeighbors) outFile << " " << v;
            outFile << "\n";
        } else {
            outFile << u << ":\n";
        }
        if (insideNeighbors.size() > 0) { outFile3 << u << ":"; for (int v : insideNeighbors) outFile3 << " " << v; outFile3 << "\n"; }
        else { outFile3 << u << ":\n"; }
    }

    std::cout << "# Original Node Adjacency List (internal edges of supernodes removed)\n";
    std::cout << "# Format: NodeID: Neighbor1 Neighbor2 ...\n";
    std::cout << "# Total remaining nodes: " << remainingNodes << "\n";
    outFile.close();
    outFile3.close();

    // 恢复: gt_input_edges.txt + id_mapping.txt (GT 编码输入)
    buildPureEdgeGraphFromCollectedData(basepath, externalEdges, nodesWithExternalEdges);
}



//新增pureedge 
void Graph::buildPureEdgeGraphFromCollectedData(
    const std::string& outputPath,
    const std::vector<std::pair<int, int>>& externalEdges,
    const std::unordered_set<int>& nodesWithExternalEdges) {
    
    // 重新编号
    std::unordered_map<int, int> nodeMapping;
    int newNodeId = 0;
    
    // 为有外部边的节点分配新ID
    for (int node : nodesWithExternalEdges) {
        nodeMapping[node] = newNodeId++;
    }
    
    // 输出去重后的边列表（无向图只保留 u < v）
    std::set<std::pair<int, int>> uniqueEdges;
    for (const auto& [u, v] : externalEdges) {
        int nu = nodeMapping[u];
        int nv = nodeMapping[v];
        if (nu < nv) {
            uniqueEdges.insert({nu, nv});
        } else {
            uniqueEdges.insert({nv, nu});
        }
    }
    
    // 写入文件
    remainingNodes = nodesWithExternalEdges.size();   // 恢复: summary Total_remain_nodes = newgraph 表头节点数
    remainingEdges = uniqueEdges.size();  // 省略内部边后、超节点邻接去重的无向边数（=gt_input_edges.txt 表头边数）
    std::ofstream outFile(outputPath + "/gt_input_edges.txt");
    outFile << nodesWithExternalEdges.size() << " " << uniqueEdges.size() << "\n";
    for (const auto& [u, v] : uniqueEdges) {
        outFile << u << " " << v << "\n";
    }
    outFile.close();
    
    // 输出映射文件
    std::ofstream mappingFile(outputPath + "/id_mapping.txt");
    mappingFile << "# NewNodeID OldNodeID \n";//顺序可能混淆但不用改
    // 维持原样
    for (const auto& [newId, oldId] : nodeMapping) {
        mappingFile << newId << " " << oldId << "\n";
    }
    mappingFile.close();
}
