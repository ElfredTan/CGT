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
    std::vector<int> neighbors;  // sorted, supports binary_search
};

// Shape synopsis (one per non-single supernode; singles store none)
struct Synopsis {
    // super_node_id removed — identical to the array index (slot = id); validity flagged by type!=0 (default 0 = invalid)
    uint8_t type = 0;         // 0=invalid/empty slot 1=clique 2=star 3=diamond 4=path (5 reserved for single)
    int size;
    int max_degree;
    int center_or_pathbegin;  // star center or path head (begin slot)
    int pathend;              // path tail / diamond base edge v (end slot)

    Synopsis() : size(0), max_degree(0),
                 center_or_pathbegin(-1), pathend(-1) {}
};

class Graph {
public:

    // Pure-edge graph builder from collected data
    void buildPureEdgeGraphFromCollectedData(
        const std::string& outputPath,
        const std::vector<std::pair<int, int>>& externalEdges,
        const std::unordered_set<int>& nodesWithExternalEdges);

    std::vector<Node> nodes;
    std::vector<std::vector<int>> supernodes_members;
    std::vector<Synopsis> synopsis_list;

    std::vector<int> fC; // node id -> supernode id (-1 = not contracted)
    std::vector<int> m_degree;   // degree cache shared during contraction
    std::unordered_map<int, std::unordered_set<int>> contractedEdges; // supernode id -> set of neighbor supernodes

    void loadEdgeFormat(const std::string& filename);

    // GCON
    // typeOrder: shape names and order; default all four clique->diamond->star->path
    void contract(long timestamp_threshold, int kl, int ku,
                  const std::vector<std::string>& typeOrder = {"clique","star","diamond","path"});

    void buildContractedGraphWithoutInternalEdges(const std::string& basepath);
    void buildSuperGraphAdjacency();   // standalone supergraph adjacency build (for profiling)

    void wrapRemainingAsSingletonSupernodes();
    void exportContractedGraph(const std::string& basepath,const std::string& dataset_name);
    void exportPathPos(const std::string& basepath);   // intra-path position map (path_pos.txt)

    void buildSynopsisFlat(int super_node_id, const std::string& type,
                           const std::vector<int>& members, int start_node);

    int getNodeCount() const { return nodes.size(); }

    void contractTopologicalComponents(int kl, int ku, const std::vector<std::string>& typeOrder);
    void contractSpecificType(int kl, int ku, const std::string& type, int& supernode_id);

    // Per-shape grow functions
    bool growCliqueFrom(int start, std::vector<bool> &visited, std::vector<int> &group,int kl,int ku);
    bool growStarFrom(int center, std::vector<bool> &visited, std::vector<int> &group,int kl,int ku);
    bool growPathFrom(int start, std::vector<bool> &visited, std::vector<int> &group,int kl,int ku);
    bool growDiamondFrom(int start, std::vector<bool> &visited, std::vector<int> &group,int kl,int ku);

private:    
};

#endif // GRAPH_H
