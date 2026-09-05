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
std::map<std::string, int> typeNodeCounts; // node count per shape type

int global_contracted_no_single_node_count = 0;
int remainingNodes = 0;
int supernode_id = 0;

// Globally compact: supernodes numbered contiguously
long long nodenum=-1, edgenum=-1;
long long remainingEdges = -1;
void Graph::loadEdgeFormat(const std::string& filename) {
    std::ifstream file(filename);
    if (!file) throw std::runtime_error("Cannot open file: " + filename);

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

    // Second pass: fill edge data
    std::string line;
    int edge_count = 0;
    while (getline(file, line)) {
        std::istringstream line_iss(line);
        int src, dst;

        if (!(line_iss >> src >> dst)) {
            std::cerr << "Warning: Invalid line format: " << line << std::endl;
            continue;
        }
        
        if (src == dst) continue;
        
        if (src < 0 || src >= nodenum || dst < 0 || dst >= nodenum) {
            std::cerr << "Warning: Node ID out of range: " << src << " " << dst << std::endl;
            continue;
        }

        nodes[src].neighbors.push_back(dst);
        nodes[dst].neighbors.push_back(src);
        nodes[src].id = src;
        nodes[dst].id = dst;
        edge_count++;
    }

    // Sort all neighbor lists (binary_search/intersection below depend on it)
    for (auto& node : nodes) {
        if (!node.neighbors.empty()) {
            std::sort(node.neighbors.begin(), node.neighbors.end());
        }
    }
    
    std::cout << "Loaded " << nodenum << " nodes and " << edge_count << " edges from " << filename << std::endl;
}

// Build the flat shape synopsis (start_node: star center or path head)
void Graph::buildSynopsisFlat(int super_node_id, const std::string& type,
                                const std::vector<int>& members, int start_node) {

    Synopsis& syn = synopsis_list[super_node_id];
    syn.type = static_cast<uint8_t>(typeToCode(type));   // uint8 in memory (id removed, slot = id)
    syn.size = members.size();
    syn.center_or_pathbegin = start_node;
    syn.pathend = -1;

    int max_deg = 0;
    for (int member : members) {
        const auto& node = nodes[member];
        max_deg = std::max(max_deg, static_cast<int>(node.neighbors.size()));
    }
    syn.max_degree = max_deg;

    if (type == "path") {
        syn.center_or_pathbegin = members.front();
        syn.pathend = members.back();
    } else if (type == "diamond") {   // 3: pathend = base edge v end (begin already holds u)
        // diamond member order [u,v,x,y]: u=start (stored in center_or_pathbegin), v = other end of the base edge
        syn.pathend = members[1];
    }
}

// contractTopologicalComponents: contract topological components in the given typeOrder
void Graph::contractTopologicalComponents(int kl, int ku, const std::vector<std::string>& typeOrder) {
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
    if (start < 0 || start >= localVisited.size() ||
        (start < fC.size() && fC[start] >= 0) || localVisited[start] ||
        start >= nodes.size()) return false;
    
    
    group.clear();
    group.push_back(start);
    std::vector<int> candidates;

    for (int nb : nodes[start].neighbors) {
        if (nb >= 0 && nb < localVisited.size() &&
            !(nb < fC.size() && fC[nb] >= 0) && !localVisited[nb] &&
            nb < nodes.size()) {
            candidates.push_back(nb);
        }

    }

    // Coarse lower-bound check: kl=4 needs >=3 qualifying neighbors; prune early
    if(candidates.size() < kl-1) return false;
    

    std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
        return m_degree[a] > m_degree[b];
    });

    std::vector<int> common_sorted = candidates;
    std::sort(common_sorted.begin(), common_sorted.end());   // id ascending

    // Grow the largest possible clique step by step
    for (int cand : candidates) {
        if (m_degree[cand] < group.size()) break;

        // cand adjacent to the whole group iff cand is in common_sorted
        bool isClique = std::binary_search(common_sorted.begin(), common_sorted.end(), cand);
        if (isClique) {
            group.push_back(cand);
            // stop at the ku upper bound
            if(group.size()==ku)
            {
                break;
            }
            // after push, update common = common ∩ neighbors(cand) (intersection of two sorted vectors)
            intersectInPlace(common_sorted, nodes[cand].neighbors);
        }
    }

    // need at least 4 nodes (start included)
    if (group.size() < kl) return false;

    for (int v : group) localVisited[v] = true;
    return true;
}

// Nodes unfit as centers may still serve as leaves; exclude locally first, never globally up front
bool Graph::growStarFrom(int center, std::vector<bool>& localVisited, std::vector<int>& group,int kl,int ku) {
    if (center < 0 || center >= localVisited.size() ||
        (center < fC.size() && fC[center] >= 0) || localVisited[center] ||
        center >= nodes.size()) return false;

    // Phase 1: collect candidate leaves, preserving traversal order
    std::vector<int> potential_leaves;
    for (int nb : nodes[center].neighbors) {
        if (!(nb < fC.size() && fC[nb] >= 0) && !localVisited[nb] && nb < nodes.size()) {
            potential_leaves.push_back(nb);
        }
        if(potential_leaves.size()==ku-1)
        {
            break;
        }
    }

    if (potential_leaves.size() < static_cast<size_t>(kl - 1)) {
        return false;
    }

    std::sort(potential_leaves.begin(), potential_leaves.end(), [this](int a, int b) {
        return m_degree[a] < m_degree[b];
    });

    // Phase 2: try adding leaves one by one, checking non-adjacency against already-added leaves only
    std::vector<int> selected_leaves;

    for (int leaf : potential_leaves) {
        if (selected_leaves.empty()) {
            selected_leaves.push_back(leaf);
            continue;
        }

        bool can_add = true;
        for (int selected : selected_leaves) {
            // neighbor lists are symmetric (loadEdgeFormat adds both directions), one lookup suffices
            if (std::binary_search(nodes[leaf].neighbors.begin(), nodes[leaf].neighbors.end(), selected)) {
                can_add = false;
                break;
            }
        }

        if (can_add) {
            selected_leaves.push_back(leaf);

            if (selected_leaves.size() >= static_cast<size_t>(ku - 1)) {
                break;
            }
        }
    }

    // Phase 3: check the size against the threshold
    if (selected_leaves.size() >= static_cast<size_t>(kl - 1)) {
        group.push_back(center);
        group.insert(group.end(), selected_leaves.begin(), selected_leaves.end());

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

    // start must have degree exactly 2
    if (m_degree[start] != 2)
        return false;

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

    // Grow the chain from start in both directions
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
            // cycle detection
            if (groupSet.count(curr))
            {
                return;
            }

            chain.push_back(curr);
            groupSet.insert(curr);

            size_t totalSize =
                leftChain.size() +
                rightChain.size() +
                1;

            if (totalSize >= static_cast<size_t>(ku))
            {
                return;
            }

            // Interior node condition: global degree must be 2
            if (m_degree[curr] != 2)
            {
                return;
            }

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

            if (nextNode == -1)
            {
                return;
            }

            prev = curr;
            curr = nextNode;
        }
    };

    // leftward
    extendDirection(
        nearneighbor[0],
        start,
        leftChain);

    // rightward
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

        // if the endpoints are adjacent (a cycle), trim the higher-degree endpoint
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

    if (group.size() < static_cast<size_t>(kl))
    {
        return false;
    }

    for (int v : group)
    {
        localVisited[v] = 1;
    }

    return true;
}

// diamond: pick edge (u,v), then x,y with (a) both adjacent to u and v, (b) x,y non-adjacent.
// start plays u; iterate its eligible neighbors as v; fixed 4-node structure.
bool Graph::growDiamondFrom(int start, std::vector<bool>& localVisited, std::vector<int>& group,int kl,int ku) {
    // Initial checks (same as clique)
    if (start < 0 || start >= localVisited.size() ||
        (start < fC.size() && fC[start] >= 0) || localVisited[start] ||
        start >= nodes.size()) return false;

    std::vector<int> candidates;
    for (int nb : nodes[start].neighbors) {
        if (nb >= 0 && nb < localVisited.size() &&
            !(nb < fC.size() && fC[nb] >= 0) && !localVisited[nb] &&
            nb < nodes.size()) {
            candidates.push_back(nb);
        }
    }

    // a diamond needs 4 nodes: u needs >=1 neighbor v and >=2 common neighbors
    // no precise early pruning; try each v
    if (candidates.size() < 1) return false;

    // try high-degree v first (more common neighbors, easier to complete a diamond)
    std::sort(candidates.begin(), candidates.end(), [this](int a, int b) {
        return m_degree[a] > m_degree[b];
    });

    for (int v : candidates) {
        std::vector<int> common;
        for (int nb : nodes[start].neighbors) {
            if (nb == v) continue;
            if (nb < 0 || nb >= localVisited.size() ||
                (nb < fC.size() && fC[nb] >= 0) || localVisited[nb] ||
                nb >= nodes.size()) {
                continue;
            }
            if (std::binary_search(nodes[v].neighbors.begin(), nodes[v].neighbors.end(), nb)) {
                common.push_back(nb);
            }
        }

        if (common.size() < 2) continue;

        bool found = false;
        int x = -1, y = -1;
        for (size_t i = 0; i < common.size() && !found; ++i) {
            for (size_t j = i + 1; j < common.size() && !found; ++j) {
                int a = common[i];
                int b = common[j];
                // (a) x,y non-adjacent (checked via the sorted neighbor lists)
                // neighbor lists are symmetric, one lookup suffices
                bool connected = std::binary_search(nodes[a].neighbors.begin(), nodes[a].neighbors.end(), b);
                if (!connected) {
                    x = a;
                    y = b;
                    found = true;
                }
            }
        }

        if (!found) continue;

        // Build the group: u=start, v, x, y (4 nodes)
        // start is not necessarily in the group yet; clear then push
        group.clear();
        group.push_back(start);
        group.push_back(v);
        group.push_back(x);
        group.push_back(y);

        // fixed 4 nodes; kl/ku only bound from below for diamond (requires kl<=4)
        if (static_cast<int>(group.size()) < kl) return false;

        for (int vtx : group) localVisited[vtx] = true;
        return true;
    }

    return false;
}

void Graph::contractSpecificType(int kl, int ku, const std::string& type, int& supernode_id) {
    size_t max_node_id = nodes.size() - 1;

    std::vector<bool> localVisited(max_node_id + 1, false);

    std::vector<int> sortedIds;
    for (size_t id = 0; id < nodes.size(); ++id) {
        if (id >= localVisited.size() ||
            (id < fC.size() && fC[id] >= 0) || localVisited[id]) {
            continue;
        }
        sortedIds.push_back(id);
    }

    std::sort(sortedIds.begin(), sortedIds.end(), [&](int a, int b) {
        return m_degree[a] > m_degree[b];
    });

    // Iterate sorted nodes
    for (int id : sortedIds) {
        // id is the grow start vertex
        if (id < 0 || id >= localVisited.size() ||
            (id < fC.size() && fC[id] >= 0) || localVisited[id]) {
            continue;
        }

        std::vector<int> group;
        bool success = false;

        // Timing: grow search (per shape + total)
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
            // globallyContracted is expressed by fC[v]>=0 (set via fC[v]=sn.id below)
            int current_supernode_id = supernode_id++;

            for (int v : group) {
                fC[v] = current_supernode_id;
            }

            supernodes_members[current_supernode_id] = group;

            typeCounts[type]++;
            typeNodeCounts[type] += group.size();
            buildSynopsisFlat(current_supernode_id, type, group, id);

        }
    }
}

// exportContractedGraph: write all contraction outputs (super_adj/mapping/synopsis/members/path_pos/summary)
void Graph::exportContractedGraph(const std::string& basepath,const std::string& dataset_name) {
    std::cout << "Exporting FULL contracted graph to: " << basepath << std::endl;
    
    struct stat info;
    if (stat(basepath.c_str(), &info) != 0) {
        std::string cmd = "mkdir -p " + basepath;
        if (system(cmd.c_str()) != 0) {
            std::cerr << "Error: Failed to create directory " << basepath << std::endl;
            return;
        }
    }

    contractedEdges.clear(); 

    for (size_t srcId = 0; srcId < nodes.size(); ++srcId) {
        if (srcId >= fC.size() || fC[srcId] < 0) continue;

        int superSrc = fC[srcId];
        for (int dstId : nodes[srcId].neighbors) {
            if (dstId >= fC.size() || fC[dstId] < 0) continue;
            if (srcId > static_cast<size_t>(dstId)) continue; // each undirected edge once

            int superDst = fC[dstId];
            if (superSrc != superDst) {
                contractedEdges[superSrc].insert(superDst);   // supergraph adjacency
                contractedEdges[superDst].insert(superSrc);
            }
        }
    }

    // 3. super_adj.txt
    std::ofstream gfile(basepath + "/super_adj.txt");
    if (!gfile.is_open()) throw std::runtime_error("Cannot open super_adj.txt");

    gfile << "SuperNodeID Edges\n";
    for (int id = 0; id < supernode_id; id++) {
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

    // 6. mapping.txt
    //    downstream loadMappingTxt fills nodeToSuper (indexed by origNode) and is order-insensitive within a supernode, so emit in members order
    //    path/diamond member order is inherently preserved (group order)
    std::ofstream mappingFile(basepath + "/mapping.txt");
    if (!mappingFile.is_open()) throw std::runtime_error("Cannot open mapping.txt");

    mappingFile << "OriginalNodeID\tSuperNodeID\tSuperNodeType\n";
    for (int sid = 0; sid < supernode_id; ++sid) {
        if (sid >= static_cast<int>(supernodes_members.size()) || supernodes_members[sid].empty()) continue;
        // type: non-single from synopsis_list; singles (sid>=singlemin or invalid slot) hardcoded to 5
        int typecode;
        if (sid < static_cast<int>(synopsis_list.size()) && synopsis_list[sid].type != 0) {
            typecode = synopsis_list[sid].type;   // already uint8; valid iff type!=0
        } else {
            typecode = 5;  // single or out-of-range slot
        }
        for (int nid : supernodes_members[sid]) {
            mappingFile << nid << "\t" << sid << "\t" << typecode << "\n";
        }
    }
    mappingFile.close();

    // 7. synopsis.txt
    std::ofstream synopsisFile(basepath + "/synopsis.txt");
    if (!synopsisFile.is_open()) throw std::runtime_error("Cannot open synopsis.txt");

    // synopsis format: no parenthesized type, no Members, no singles, numeric type=
    for (size_t id = 0; id < synopsis_list.size(); ++id) {
        if (id >= supernodes_members.size() || supernodes_members[id].empty()) {
            continue;
        }
        const auto& syn = synopsis_list[id];
        if (syn.type == 0) continue;   // skip empty/unfilled slots (type=0), validity via type

        synopsisFile << "SuperNode " << id << " Synopsis{";
        synopsisFile << "type=" << static_cast<int>(syn.type);
        synopsisFile << ",size=" << syn.size;
        synopsisFile << ",md=" << syn.max_degree;

        // begin/end = the two special nodes of a shape (generic slots, meaning per type):
        //   star(2):    begin=center, no end (no separate c=; center goes in begin)
        //   path(4):    begin/end = chain ends
        //   diamond(3): begin/end = base edge (u,v)
        //   clique(1):  no special nodes, not written
        if (syn.type == 2) {            // star: write begin (=center) only
            synopsisFile << ",begin=" << syn.center_or_pathbegin;
        } else if (syn.type == 4 || syn.type == 3) {   // path/diamond: write both ends
            synopsisFile << ",begin=" << syn.center_or_pathbegin;
            synopsisFile << ",end=" << syn.pathend;
        }
        synopsisFile << "}\n";
    }
    synopsisFile.close();

    // 7b. members.txt (fC': supernode -> member array, same layout as super_adj.txt)
    //     includes singles; path/diamond order preserved by iterating supernodes_members[id] directly
    std::ofstream membersFile(basepath + "/members.txt");
    if (!membersFile.is_open()) throw std::runtime_error("Cannot open members.txt");
    membersFile << "# SuperNodeID member1 member2 ...\n";
    // bound switched to supernodes_members.size() — synopsis_list was shrunk to non-singles and can no longer bound members
    for (size_t id = 0; id < supernodes_members.size(); ++id) {
        if (supernodes_members[id].empty()) continue;
        membersFile << id;
        for (int m : supernodes_members[id]) membersFile << " " << m;
        membersFile << "\n";
    }
    membersFile.close();

    // Export the intra-path position map (for O(1) intra-path edge queries downstream)
    exportPathPos(basepath);

    // 8. summary.txt
    std::ofstream summaryFile(basepath + "/summary.txt");
    if (!summaryFile.is_open()) throw std::runtime_error("Cannot open summary.txt");
    summaryFile << "Dataset_name: " << dataset_name << "\n";
    summaryFile << "Total_ori_nodes: " << nodes.size() << "\n";
    summaryFile << "Total_ori_edges: " << edgenum << "\n";

    summaryFile << "Total_super_nodes: " << supernode_id << "\n";
    summaryFile << "Total_super_edges: " << edgeCount << "\n";

    summaryFile << "Total_remain_nodes: " << remainingNodes << "\n";
    summaryFile << "Total_remain_edges: " << remainingEdges << "\n";  // edge count after dropping internal edges (= gt_input_edges.txt header)

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

// wrapRemainingAsSingletonSupernodes: wrap remaining uncontracted nodes as single-node supernodes
void Graph::wrapRemainingAsSingletonSupernodes() {
    for (size_t nid = 0; nid < nodes.size(); ++nid) {
        if (nid >= fC.size() || fC[nid] < 0) {
            int current_id = supernode_id++;
            fC[nid] = current_id;
            // singles are not stored in synopsis_list (only fC/fC' are filled); arrays were sized in loadEdgeFormat
            // store the member (fC' forward)
            supernodes_members[current_id] = {static_cast<int>(nid)};

            // typeCounts hardcoded, independent of synopsis_list
            typeCounts["single"]++;
        }
    }
}

// contract: main contraction entry
void Graph::contract(long timestamp_threshold, int kl, int ku, const std::vector<std::string>& typeOrder) {
    supernode_id = 0;
    contractedEdges.clear();

    fC.assign(nodes.size(), -1);  // -1 = not contracted

    // Precompute the degree cache (nodes is read-only during contraction; shared by all four shapes)
    m_degree.assign(nodes.size(), 0);
    for (size_t i = 0; i < nodes.size(); ++i) m_degree[i] = static_cast<int>(nodes[i].neighbors.size());

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

    std::cout << "\n[Result]";
    std::cout << "\n - Total nodes: " << nodes.size();
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
    std::cout << "\n[Validation] Supernode details:\n";

    wrapRemainingAsSingletonSupernodes();
    {
        long long singleCnt = 0;
        auto it = typeCounts.find("single");
        if (it != typeCounts.end()) singleCnt = it->second;
        // (1) synopsis: keep non-single slots only (singles store no synopsis)
        synopsis_list.resize(static_cast<size_t>(supernode_id - singleCnt));
        synopsis_list.shrink_to_fit();
        // (2) members: keep every supernode (singles included) — required by members.txt / downstream loadMembersTxt
        supernodes_members.resize(supernode_id);
        supernodes_members.shrink_to_fit();
    }
}

// Export the intra-path position map path_pos.txt (one "node pos" per line, path nodes only)
void Graph::exportPathPos(const std::string& basepath) {
    std::ofstream ppfile(basepath + "/path_pos.txt");
    if (!ppfile.is_open()) throw std::runtime_error("Cannot open path_pos.txt");
    long long cnt = 0, npaths = 0;
    for (size_t sid = 0; sid < supernodes_members.size(); ++sid) {
        const auto& ms = supernodes_members[sid];
        if (ms.empty()) continue;
        if (synopsis_list[sid].type != 4) continue;   // 4 = path (typeToCode)
        if (ms.size() < 2) continue;
        // Note: a few single wrappers reuse leftover type=4 synopsis slots (273 on BK);
        //     filtered by members.size()<2 so the export matches the synopsis.txt count (17701) exactly.
        ++npaths;
        for (size_t i = 0; i < ms.size(); ++i)
            ppfile << ms[i] << " " << i << "\n";
        cnt += (long long)ms.size();
    }
    ppfile.close();
    std::cout << "[INFO] path_pos.txt: " << npaths << " paths, "
              << cnt << " path nodes exported" << std::endl;
}

// buildContractedGraphWithoutInternalEdges: build the contracted graph without internal edges
// also builds the supergraph adjacency standalone (contractedEdges, O(E) verification); superEdgeToEdges (output only) excluded
void Graph::buildSuperGraphAdjacency() {
    contractedEdges.clear();
    for (size_t srcId = 0; srcId < nodes.size(); ++srcId) {
        if (srcId >= fC.size() || fC[srcId] < 0) continue;
        int superSrc = fC[srcId];
        for (int dstId : nodes[srcId].neighbors) {
            if (dstId >= fC.size() || fC[dstId] < 0) continue;
            if (srcId > static_cast<size_t>(dstId)) continue;   // each undirected edge once
            int superDst = fC[dstId];
            if (superSrc != superDst) {
                contractedEdges[superSrc].insert(superDst);
                contractedEdges[superDst].insert(superSrc);
            }
        }
    }
}

void Graph::buildContractedGraphWithoutInternalEdges(const std::string &basepath) {

    remainingNodes = 0;
    std::vector<std::pair<int, int>> externalEdges;          // all external edges
    std::unordered_set<int> nodesWithExternalEdges;          // nodes incident to external edges
    std::vector<std::vector<int>> validPerNode(nodes.size());   // vector instead of set (sort+unique afterwards)
    std::vector<std::vector<int>> insidePerNode(nodes.size());  // per-node internal neighbors (set -> vector)

    // ===== Phase 1: build (timed, no file writes) =====
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
        // sort+unique as done for adjoutside
        std::sort(validNeighbors.begin(), validNeighbors.end());
        validNeighbors.erase(std::unique(validNeighbors.begin(), validNeighbors.end()), validNeighbors.end());
        if(nodes[u].neighbors.empty()) {
            remainingNodes++;   // stdout only; summary Total_remain_nodes comes from the newgraph header
        }
        insidePerNode[u] = std::move(insideNeighbors);
        validPerNode[u] = std::move(validNeighbors);
    }

    // ===== Phase 2: write (untimed) =====
    std::string outputPath1 = basepath + "/external_adj.txt";
    std::string outputPath2 = basepath + "/internal_adj.txt";
    std::ofstream outFile(outputPath1);
    if (!outFile.is_open()) throw std::runtime_error("Cannot open output file: " + outputPath1);
    std::ofstream outFile3(outputPath2);
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

    // gt_input_edges.txt + id_mapping.txt (GT encoding inputs)
    buildPureEdgeGraphFromCollectedData(basepath, externalEdges, nodesWithExternalEdges);
}

void Graph::buildPureEdgeGraphFromCollectedData(
    const std::string& outputPath,
    const std::vector<std::pair<int, int>>& externalEdges,
    const std::unordered_set<int>& nodesWithExternalEdges) {
    
    std::unordered_map<int, int> nodeMapping;
    int newNodeId = 0;
    
    for (int node : nodesWithExternalEdges) {
        nodeMapping[node] = newNodeId++;
    }
    
    // emit the deduplicated edge list (u < v for undirected)
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
    
    remainingNodes = nodesWithExternalEdges.size();   // summary Total_remain_nodes = newgraph header node count
    remainingEdges = uniqueEdges.size();  // undirected edges after dropping internal edges and dedup across supernodes (= gt_input_edges.txt header)
    std::ofstream outFile(outputPath + "/gt_input_edges.txt");
    outFile << nodesWithExternalEdges.size() << " " << uniqueEdges.size() << "\n";
    for (const auto& [u, v] : uniqueEdges) {
        outFile << u << " " << v << "\n";
    }
    outFile.close();
    
    std::ofstream mappingFile(outputPath + "/id_mapping.txt");
    mappingFile << "# NewNodeID OldNodeID \n";
    for (const auto& [newId, oldId] : nodeMapping) {
        mappingFile << newId << " " << oldId << "\n";
    }
    mappingFile.close();
}
