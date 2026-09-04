// ========================
// File: Graph.h
// ========================
#ifndef GRAPH_H
#define GRAPH_H

#include <unordered_map>
#include <unordered_set>
#include <vector>
#include <string>
#include <map>
#include <cstdint>
#include <algorithm>

struct Node {
    int id;
    std::vector<int> neighbors;  // 排序后，支持 binary_search
};

// struct SuperNode {
//     int id;
//     // std::string type;
//     // std::unordered_set<int> members;
//     // std::unordered_map<std::string, std::string> synopsis;
//     // // 新增：记录与其他超级节点的原始边详情
//     std::map<int, std::vector<std::pair<int, int>>> originalEdges; // <superB, [(a,b),...]>
// };

// 新的摘要结构体（用于优化）
struct Synopsis {
    // 0815: super_node_id 已删 —— ≡数组下标(槽位即id), 有效标记改用 type!=0(默认0=无效)
    uint8_t type = 0;         // 0=无效/空槽 1=clique 2=star 3=diamond 4=path (5=保留single语义)
    int size;
    int max_degree;
    int center_or_pathbegin;  // star的center或path的begin (begin槽位)
    int pathend;              // path的end / diamond基础边v端 (end槽位)

    Synopsis() : size(0), max_degree(0),
                 center_or_pathbegin(-1), pathend(-1) {}
};



class Graph {
public:

    
        // 新增：基于已收集数据的纯边图构建函数
    void buildPureEdgeGraphFromCollectedData(
        const std::string& outputPath,
        const std::vector<std::pair<int, int>>& externalEdges,
        const std::unordered_set<int>& nodesWithExternalEdges);

    std::vector<Node> nodes;
    std::vector<std::vector<int>> supernodes_members;
    std::vector<Synopsis> synopsis_list;

    std::vector<int> fC; // node id -> supernode id (-1 表示未收缩)
    std::vector<int> m_degree;   // 
    std::unordered_map<int, std::unordered_set<int>> contractedEdges; // supernode id -> set of neighbor supernodes


    void loadEdgeFormat(const std::string& filename);

    // GCON
    // typeOrder: 收缩形状的名称与顺序，默认全部四种 clique->diamond->star->path
    void contract(long timestamp_threshold, int kl, int ku,
                  const std::vector<std::string>& typeOrder = {"clique","star","diamond","path"});

    void buildContractedGraphWithoutInternalEdges(const std::string& basepath);
    void buildSuperGraphAdjacency();   // 0813: 独立测超图邻接表构建




    void wrapRemainingAsSingletonSupernodes();
    void exportContractedGraph(const std::string& basepath,const std::string& dataset_name);
    void exportPathPos(const std::string& basepath);   // 0905: path 内部位置映射 (path_pos.txt)

    void buildSynopsisFlat(int super_node_id, const std::string& type,
                           const std::vector<int>& members, int start_node);

        // 新增两个基础查询方法
    int getNodeCount() const { return nodes.size(); }

    void contractTopologicalComponents(int kl, int ku, const std::vector<std::string>& typeOrder);
    void contractSpecificType(int kl, int ku, const std::string& type, int& supernode_id);


    //各种形状的grow
    bool growCliqueFrom(int start, std::vector<bool> &visited, std::vector<int> &group,int kl,int ku);
    bool growStarFrom(int center, std::vector<bool> &visited, std::vector<int> &group,int kl,int ku);
    bool growPathFrom(int start, std::vector<bool> &visited, std::vector<int> &group,int kl,int ku);
    bool growDiamondFrom(int start, std::vector<bool> &visited, std::vector<int> &group,int kl,int ku);

private:    
};

#endif // GRAPH_H
