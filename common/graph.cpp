#include "graph.h"
#include <fstream>
#include <sstream>
#include <iostream>
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <climits>

using namespace std;
using Node = int;

// ========== Graph globals (shared by the three algorithms) ==========
// Algorithm-specific globals

int numRemainNodes = -1;
int numOriNodes = -1;
int supernodenum = -1;

vector<int> oldIdOfNew;
vector<vector<int>> adjExternal;
vector<vector<int>> adjInternal;
vector<vector<int>> adjMerged;
vector<int> nodeToSuper;
vector<vector<int>> superNodeMembers;
vector<SuperNode> superNodes;
vector<vector<int>> superAdj;

// smallest single-supernode id (singles are contiguous at the end of synopsis.txt)
// id >= firstSingleId is a single supernode (no synopsis, treated as an original node); INT_MAX means none.
int firstSingleId = INT_MAX;

vector<BitCode> gtCode;
vector<BitCode> gtMask;   // product of computeGtMask
vector<int> globalDegree;

// ========== Helpers ==========

vector<string> readLines(const string& filename) {
    ifstream fin(filename);
    if (!fin) {
        cout << "Error opening: " << filename << endl;
        exit(1);
    }
    vector<string> lines;
    string line;
    while (getline(fin, line)) {
        if (!line.empty()) lines.push_back(line);
    }
    return lines;
}

string trim(const string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    if (start == string::npos) return "";    size_t end = s.find_last_not_of(" \t\r\n");
    return s.substr(start, end - start + 1);
}


void readSummaryStats(const string& filepath) {
    ifstream infile(filepath);

    if (!infile.is_open()) {
        throw runtime_error("Cannot open file: " + filepath);
    }

    string line;

    while (getline(infile, line)) {

        if (line.empty() || line[0] == '#') {
            continue;
        }

        size_t colon_pos = line.find(':');

        if (colon_pos == string::npos) {
            continue;
        }

        string key = trim(line.substr(0, colon_pos));
        string value_str = trim(line.substr(colon_pos + 1));

        if (key == "Dataset_name") {
            continue;
        }

        int value;

        try {
            value = stoi(value_str);
        }
        catch (const exception& e) {
            cerr << "stoi failed in summary.txt\n";
            cerr << "key   = [" << key << "]\n";
            cerr << "value = [" << value_str << "]\n";
            throw;
        }

        if (key == "Total_ori_nodes") {
            numOriNodes = value;
        }
        else if (key == "Total_super_nodes") {
            supernodenum = value;
        }
        else if (key == "Total_remain_nodes") {
            numRemainNodes = value;
        }
    }

    cout << "Summary loaded:\n";
    cout << "numOriNodes      = " << numOriNodes << endl;
    cout << "supernodenum    = " << supernodenum << endl;
    cout << "numRemainNodes         = " << numRemainNodes << endl;
}
void loadIdMapping(const string& filename) {
    ifstream fin(filename);
    if (!fin.is_open()) {
        cerr << "Cannot open file: " << filename << "\n";
        return;
    }

    oldIdOfNew.clear();
    oldIdOfNew.resize(numRemainNodes);
    string line;
    int lineNum = 0;

    while (getline(fin, line)) {
        lineNum++;
        string trimmed = trim(line);
        if (trimmed.empty()) continue;         if (trimmed[0] == '#') continue;
        stringstream ss(trimmed);
        int oldID, newID;
        if (!(ss >> oldID >> newID)) {
            cerr << "Parse error (line " << lineNum << "): " << trimmed << "\n";
            continue;
        }

        oldIdOfNew[newID] = oldID;
        if (oldID == 0) {
            cout << "Found mapping for oldID 0: newID = " << newID << "\n";
        }
    }
    fin.close();
}

void loadMappingTxt(const string& filename) {
    nodeToSuper.clear();
    nodeToSuper.resize(numOriNodes);
    auto lines = readLines(filename);

    for (auto& line : lines) {
        istringstream iss(line);
        int origNode, superNode, typetag;
        iss >> origNode >> superNode >> typetag;

        nodeToSuper[origNode] = superNode;
    }
}

// fC' supernode -> member array (from members.txt, incl. singles, path/diamond order-preserving)
void loadMembersTxt(const string& filename) {
    superNodeMembers.clear();
    superNodeMembers.resize(supernodenum);
    auto lines = readLines(filename);
    for (auto& line : lines) {
        istringstream iss(line);
        int sid, m;
        if (!(iss >> sid)) continue;
        while (iss >> m) superNodeMembers[sid].push_back(m);
    }
}

void loadExternalAdj(const string& filename) {
    auto lines = readLines(filename);
    for (auto& line : lines) {
        if (line.empty()) continue;

        istringstream iss(line);
        int u, v;
        char colon;
        if (!(iss >> u >> colon)) {
            cerr << "Unparsable line: " << line << "\n";
            continue;
        }

        int uadj2cnt = 0;
        while (iss >> v) {
            adjExternal[u].push_back(v);
            uadj2cnt++;
        }
        globalDegree[u] = uadj2cnt;
    }
}

void loadInternalAdj(const string& filename) {
    auto lines = readLines(filename);
    for (auto& line : lines) {
        if (line.empty()) continue;

        istringstream iss(line);
        int u, v;
        char colon;
        if (!(iss >> u >> colon)) {
            cout << "Unparsable line: " << line << "\n";
            continue;
        }

        int uadj1cnt = 0;
        while (iss >> v) {
            adjInternal[u].push_back(v);
            uadj1cnt++;
        }
        globalDegree[u] += uadj1cnt;
    }
}

void loadExternalAdjHalf(const string& filename) {
    auto lines = readLines(filename);
    for (auto& line : lines) {
        if (line.empty()) continue;

        istringstream iss(line);
        int u, v;
        char colon;
        if (!(iss >> u >> colon)) {
            cerr << "Unparsable line: " << line << "\n";
            continue;
        }

        int uadj2cnt = 0;
        while (iss >> v) {
            if (v > u) {
                adjExternal[u].push_back(v);
            }
            uadj2cnt++;
        }
        globalDegree[u] += uadj2cnt;
    }
}

void loadInternalAdjHalf(const string& filename) {
    auto lines = readLines(filename);
    for (auto& line : lines) {
        if (line.empty()) continue;

        istringstream iss(line);
        int u, v;
        char colon;
        if (!(iss >> u >> colon)) {
            cout << "Unparsable line: " << line << "\n";
            continue;
        }

        int uadj1cnt = 0;
        while (iss >> v) {
            if (u < v) {
                adjInternal[u].push_back(v);
            }
            uadj1cnt++;
        }
        globalDegree[u] += uadj1cnt;
    }
}


bool g_path_pos_ok = false;

static vector<uint64_t> g_path_pos_tab;
static uint64_t g_path_pos_mask = 0;

static inline uint64_t pp_mix64(uint64_t x) {
    x += 0x9E3779B97F4A7C15ULL;
    x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5BULL;
    x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
    return x ^ (x >> 31);
}

void loadPathPos(const string& filename) {
    g_path_pos_ok = false;
    ifstream fin(filename);
    if (!fin.is_open()) {
        cout << "[WARN] " << filename << " not found, intra-path edge query falls back to O(N)" << endl;
        return;
    }
    // Pass 1: count entries to size the table (two streaming passes are cheap)
    long long n = 0;
    string line;
    while (getline(fin, line)) if (!line.empty()) ++n;
    fin.clear();
    fin.seekg(0);

    size_t cap = 16;
    while (cap < (size_t)n * 2) cap <<= 1;
    g_path_pos_tab.assign(cap, 0);
    g_path_pos_mask = cap - 1;

    while (getline(fin, line)) {
        if (line.empty()) continue;
        istringstream iss(line);
        long long node; int pos;
        if (!(iss >> node >> pos)) continue;
        uint64_t v = ((uint64_t)(uint32_t)node << 32) | (uint32_t)(pos + 1);
        uint64_t h = pp_mix64((uint32_t)node) & g_path_pos_mask;
        while (g_path_pos_tab[h]) h = (h + 1) & g_path_pos_mask;
        g_path_pos_tab[h] = v;
    }
    g_path_pos_ok = true;
}

bool queryPathPos(uint32_t u, uint32_t v) {
    if (!g_path_pos_ok) return false;
    uint64_t h = pp_mix64(u) & g_path_pos_mask;
    uint64_t found = 0;
    for (;;) {
        uint64_t s = g_path_pos_tab[h];
        if (!s) return false;              
        if ((uint32_t)(s >> 32) == u) { found = s & 0xffffffffULL; break; }
        h = (h + 1) & g_path_pos_mask;
    }
    h = pp_mix64(v) & g_path_pos_mask;
    for (;;) {
        uint64_t s = g_path_pos_tab[h];
        if (!s) return false;
        if ((uint32_t)(s >> 32) == v) {
            int32_t pu = (int32_t)found - 1, pv = (int32_t)(s & 0xffffffffULL) - 1;
            return pu >= 0 && (pu > pv ? pu - pv : pv - pu) == 1;
        }
        h = (h + 1) & g_path_pos_mask;
    }
}

// Common-bit mask of GT codes over all (external) neighbors of a node;
// smallest triangle vertex must have mask 0 (lossless prune)
void computeGtMask() {
    gtMask.resize(numOriNodes);
    for (int u = 0; u < numOriNodes; u++) {
        gtMask[u] = ~0ull;

        if (adjExternal[u].size() > 0) {
            BitCode masku = gtCode[adjExternal[u][0]];
            for (size_t i = 1; i < adjExternal[u].size(); i++) {
                masku = masku & gtCode[adjExternal[u][i]];
            }
            gtMask[u] = masku;
        }
    }
}

void buildInternalAdjFromSynopsis() {
    adjInternal.assign(numOriNodes, {});
    for (size_t sid = 0; sid < superNodes.size(); ++sid) {
        const auto& ms = superNodeMembers[sid];
        if (ms.empty()) continue;
        const SuperNode& S = superNodes[sid];

        if (S.type == 1) {                    // clique: all pairs
            for (size_t a = 0; a + 1 < ms.size(); ++a)
                for (size_t b = a + 1; b < ms.size(); ++b) {
                    adjInternal[ms[a]].push_back(ms[b]);
                    adjInternal[ms[b]].push_back(ms[a]);
                }
        } else if (S.type == 2) {             // star: center (beginnode) <-> leaves
            int c = S.beginnode;
            for (int m : ms)
                if (m != c) { adjInternal[c].push_back(m); adjInternal[m].push_back(c); }
        } else if (S.type == 3) {             // diamond: begin/end adjacent to all members (K4 minus x-y)
            int b = S.beginnode, e = S.endnode;
            for (int m : ms) {
                if (m != b) { adjInternal[b].push_back(m); adjInternal[m].push_back(b); }
                if (m != e) { adjInternal[e].push_back(m); adjInternal[m].push_back(e); }
            }
        } else if (S.type == 4) {             // path: consecutive members adjacent
            for (size_t a = 0; a + 1 < ms.size(); ++a) {
                adjInternal[ms[a]].push_back(ms[a + 1]);
                adjInternal[ms[a + 1]].push_back(ms[a]);
            }
        }
        // type==5 (single) / 0 (empty slot): no internal edges, skip
    }
    // Sort (clique all-pairs is duplicate-free; star/diamond derivation may push duplicates, dedupe order-preserving)
    for (auto& nb : adjInternal) {
        sort(nb.begin(), nb.end());
        nb.erase(unique(nb.begin(), nb.end()), nb.end());
    }
}

void mergeAdjacentLists() {    auto start = chrono::high_resolution_clock::now();

    adjMerged.clear();
    adjMerged.resize(adjInternal.size());

    for (size_t i = 0; i < adjInternal.size(); ++i) {
        adjMerged[i].reserve(adjInternal[i].size() + adjExternal[i].size());

        merge(
            adjInternal[i].begin(), adjInternal[i].end(),
            adjExternal[i].begin(), adjExternal[i].end(),
            back_inserter(adjMerged[i])
        );
    }

    auto end = chrono::high_resolution_clock::now();
    double duration = chrono::duration_cast<chrono::microseconds>(end - start).count() / 1000.0;

    cout << "[INFO] adjall built: " << adjMerged.size() << " nodes, took "
         << duration << " ms" << endl;

    long long total_edges = 0;
    for (const auto& neighbors : adjMerged) {
        total_edges += neighbors.size();
    }
    cout << "[INFO] adjall total edges: " << total_edges << endl;
}

void loadBitCodesBinary(const string& filename) {
    ifstream file(filename, ios::binary);
    if (!file) {
        cerr << "[ERROR] Cannot open binary file: " << filename << "\n";
        return;
    }

    uint32_t node_count;
    file.read(reinterpret_cast<char*>(&node_count), sizeof(node_count));
    cout << "Node count: " << node_count << "\n";

    gtCode.clear();
    gtCode.resize(numOriNodes);

    for (int i = 0; i < numOriNodes; i++) {
        gtCode[i] = ~0ull;
    }

    for (size_t i = 0; i < (size_t)numRemainNodes; ++i) {
        BitCode raw_code;
        file.read(reinterpret_cast<char*>(&raw_code), sizeof(raw_code));
        gtCode[oldIdOfNew[i]] = raw_code;
    }
}

static SuperNode parseSuperNodeLine(const string& line) {
    SuperNode sn;
    sn.beginnode = -1;

    size_t idStart = line.find("SuperNode ") + 10;
    size_t idEnd = line.find(" ", idStart);
    sn.id = stoi(line.substr(idStart, idEnd - idStart));

    size_t synStart = line.find("Synopsis{", idEnd) + 9;
    size_t synEnd = line.find("}", synStart);
    string synStr = line.substr(synStart, synEnd - synStart);

    size_t typePos = synStr.find("type=");
    sn.type = static_cast<uint8_t>(stoi(synStr.substr(typePos + 5)));   // 1..5

    size_t sizePos = synStr.find("size=");
    if (sizePos != string::npos) {
        sn.size = stoi(synStr.substr(sizePos + 5));
    }

    size_t mdsize = synStr.find("md=");
    if (mdsize != string::npos) {
        sn.md = stoi(synStr.substr(mdsize + 3));
    } else {
        sn.md = 0;
    }

    // begin/end generic slots — star: begin=center; diamond: begin/end=base edge (u,v); path: begin/end=chain ends
    size_t bPos = synStr.find("begin=");
    if (bPos != string::npos) {
        sn.beginnode = stoi(synStr.substr(bPos + 6));
        size_t ePos = synStr.find("end=");
        if (ePos != string::npos) sn.endnode = stoi(synStr.substr(ePos + 4));
    }

    return sn;
}

vector<SuperNode> loadSuperGraph(const string& filename) {
    vector<SuperNode> superGraph;
    superGraph.resize(supernodenum);
    firstSingleId = INT_MAX;  // reset: sentinel when no singles exist
    int maxNonSingleId = -1;   // synopsis has no singles; firstSingleId = maxId+1
    auto lines = readLines(filename);
    for (auto& line : lines) {
        if (line.empty()) continue;
        SuperNode sn = parseSuperNodeLine(line);
        // synopsis contains only clique/star/diamond/path (ids contiguous from 0), no single lines
        superGraph[sn.id] = sn;
        if (sn.id > maxNonSingleId) maxNonSingleId = sn.id;
    }
    // firstSingleId = first single id = non-single supernode count = maxId+1
    //       (synopsis ids are 0..firstSingleId-1 contiguous; without singles maxNonSingleId=-1 -> firstSingleId=0, yet if supernodenum>0 all are singles)
    firstSingleId = (maxNonSingleId >= 0 && maxNonSingleId + 1 < supernodenum)
                ? maxNonSingleId + 1
                : INT_MAX;
    cout << "[INFO] firstSingleId = " << firstSingleId
         << " (id >= this is a single supernode, total "
         << (supernodenum - (firstSingleId == INT_MAX ? supernodenum : firstSingleId))
         << " singles)" << endl;

    // begin/end are read directly from the synopsis (self-contained), no longer derived from superNodeMembers
    return superGraph;
}
