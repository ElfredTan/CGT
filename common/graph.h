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

// 超节点结构体
struct SuperNode {
    int id;                   // 超节点编号
    uint8_t type = 0;         // 1=clique 2=star 3=diamond 4=path 5=single
    int beginnode = -1;       // synopsis begin= 通用槽位 — star 中心 / diamond 基础边u / path 链头
    int endnode = -1;         // synopsis end= 槽位 — diamond 基础边v / path 链尾 (star/clique 不写)
    int size;                 // 成员个数
    int md;
    // unordered_set<string> labels;   // label 已停用 (统一 "user", 无标签场景; matching 的 chooseStartVertex 曾遍历)
};

// 查询图结构
struct QueryGraph {
    vector<int> node_ids;
    // vector<string> node_labels;   // label 已停用 (统一 "user", 无标签场景)
    vector<pair<int, int>> edges;
};

// NEC节点结构体
struct NECNode {
    int id;
    vector<int> vertex_set;
    NECNode* parent = nullptr;
    vector<NECNode*> children;
    int start_idx = 0;  // 添加起始索引
    int end_idx = 0;    // 添加结束索引

    // 默认构造函数
    NECNode() {}
    // 带参构造函数
    NECNode(int node_id, const vector<int>& nec_set)
        : id(node_id), vertex_set(nec_set) {}
};

// CR树类
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

// 辅助函数
vector<string> readLines(const string& filename);
string trim(const string& s);

// 图加载函数声
void readSummaryStats(const string& filepath);
void loadIdMapping(const string& filename);
void loadMappingTxt(const string& filename);
void loadMembersTxt(const string& filename);   // fC' 超节点->成员数组(从 members.txt)
vector<SuperNode> loadSuperGraph(const string& filename);
void loadExternalAdj(const string& filename);
void loadBitCodesBinary(const string& filename);
void loadInternalAdj(const string& filename);
void mergeAdjacentLists();

// 半边加载 (cda 使用: contracted_graph2/3 只读 v>u 一侧)
void loadExternalAdjHalf(const string& filename);
void loadInternalAdjHalf(const string& filename);

// GT 邻居公共位掩码 (三角形最小顶点剪枝, 无损)
void computeGtMask();

// 从 synopsis 形状摘要 + superNodeMembers(保序) 派生内部邻接表 adjInternal (全边, 升序)
void buildInternalAdjFromSynopsis();//需要时调用生成，或者收缩时生成adjinside算法读入文件

// path 内部 O(1) 查边 (path_pos.txt: node -> path 内相对位置, 收缩侧 exportPathPos 导出)
// 判定: 同一 path 内 |pos(u)-pos(v)|==1 true; else false
extern bool g_path_pos_ok;   // path_pos.txt 加载成功标记; false 时调用方回退旧 O(N) 逻辑
void loadPathPos(const string& filename);
bool queryPathPos(uint32_t u, uint32_t v);

// 全局变量声明
extern int numRemainNodes;
extern int numOriNodes;
extern int supernodenum;
extern int superedgenum;
extern int legalsuperedgesnum;

// 首个 single 超节点 id; >= firstSingleId 即为 single(无摘要), INT_MAX 表示无 single
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
extern vector<BitCode> gtMask;   // computeGtMask 产物
extern vector<int> globalDegree;

#endif // GRAPH_H
