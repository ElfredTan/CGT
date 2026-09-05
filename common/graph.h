#ifndef GRAPH_H
#define GRAPH_H

#include <iostream>
#include <vector>
#include <string>
#include <unordered_set>
#include <bitset>
#include <cstdint>

using namespace std;

const int Gbit = 64;
using BitCode = bitset<Gbit>;

// Supernode structure
struct SuperNode {
    int id;                   // supernode id
    uint8_t type = 0;         // 1=clique 2=star 3=diamond 4=path 5=single
    int beginnode = -1;       // synopsis begin= generic slot — star center / diamond base edge u / path head
    int endnode = -1;         // synopsis end= slot — diamond base edge v / path tail (not written for star/clique)
    int size;                 // member count
    int md;
    // unordered_set<string> labels;   // labels disabled (uniform "user", label-free setting; was traversed by matching chooseStartVertex)
};

// Query graph structure
struct QueryGraph {
    vector<int> node_ids;
    // vector<string> node_labels;   // labels disabled (uniform "user", label-free setting)
    vector<pair<int, int>> edges;
};

// NEC node structure
struct NECNode {
    int id;
    vector<int> vertex_set;
    NECNode* parent = nullptr;
    vector<NECNode*> children;
    int start_idx = 0;
    int end_idx = 0;

    // default constructor
    NECNode() {}
    // parameterized constructor
    NECNode(int node_id, const vector<int>& nec_set)
        : id(node_id), vertex_set(nec_set) {}
};

// CR tree class
class CRTree {
public:
    vector<vector<int>>* CR;     // the real data vertices of CRTree
    vector<int>* parent;         // parent nodes: each query node u has a set of parent nodes v (data nodes)

    void init(int num) {
        CR = new vector<vector<int>>[num];
        parent = new vector<int>[num];
    }

    ~CRTree() {
        if (CR != NULL)
            delete[] CR;
        if (parent != NULL)
            delete[] parent;
    }
};

// Helpers
vector<string> readLines(const string& filename);
string trim(const string& s);

// Graph loading functions
void readSummaryStats(const string& filepath);
void loadIdMapping(const string& filename);
void loadMappingTxt(const string& filename);
void loadMembersTxt(const string& filename);   // fC' supernode -> member array (from members.txt)
vector<SuperNode> loadSuperGraph(const string& filename);
void loadExternalAdj(const string& filename);
void loadBitCodesBinary(const string& filename);
void loadInternalAdj(const string& filename);
void mergeAdjacentLists();

// Half-edge loading (cdac reads only the v>u side)
void loadExternalAdjHalf(const string& filename);
void loadInternalAdjHalf(const string& filename);

// GT common-bit mask over neighbors (lossless prune on smallest triangle vertex)
void computeGtMask();

// Derive internal adjacency adjInternal (full edges, sorted) from synopsis shapes + superNodeMembers (order-preserving)
void buildInternalAdjFromSynopsis();// generated on demand, or by contraction and read by the algorithms

// O(1) intra-path edge query (path_pos.txt: node -> position within path, exported by contraction exportPathPos)
// true iff same path and |pos(u)-pos(v)|==1; else false
extern bool g_path_pos_ok;   // load-success flag for path_pos.txt; callers fall back to O(N) when false
void loadPathPos(const string& filename);
bool queryPathPos(uint32_t u, uint32_t v);

// Global variable declarations
extern int numRemainNodes;
extern int numOriNodes;
extern int supernodenum;

// first single-supernode id; id >= firstSingleId is single (no synopsis), INT_MAX means none
extern int firstSingleId;

extern vector<int> oldIdOfNew;
extern vector<vector<int>> adjExternal;
extern vector<vector<int>> adjInternal;
extern vector<vector<int>> adjMerged;
extern vector<int> nodeToSuper;
extern vector<vector<int>> superNodeMembers;
extern vector<SuperNode> superNodes;
extern vector<vector<int>> superAdj;

extern vector<BitCode> gtCode;
extern vector<BitCode> gtMask;   // product of computeGtMask
extern vector<int> globalDegree;

#endif // GRAPH_H
