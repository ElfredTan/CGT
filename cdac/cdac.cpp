#pragma GCC optimize(3,"Ofast","inline")
#include "cdac.h"
#include "graph.h"
#include <algorithm>
#include <chrono>
#include <iomanip>
#include <cstring>
#include <cctype>
#include <numeric>

using namespace std;

// ========== Macro switches ==========
// GT prune switch: 1 = use GT (original behavior), 0 = disable
#ifndef USE_GT
#define USE_GT 1
#endif
// Edge-check stats switch: 1 = record (original), 0 = skip (performance)
#ifndef RECORD_EDGE_STATS
#define RECORD_EDGE_STATS 1
#endif

// Bron-Kerbosch algorithm data structures (note: unrelated to the BK dataset abbreviation)
int st[3000][100000];

// Algorithm parameters
int k;
int maxCliqueNumber = -1;  // -1 sentinel (no clique), reassigned in the main loop

// Supernode related
vector<int> superNodeCsMap;

// GT prune statistics
#if RECORD_EDGE_STATS
long long no_edge_and_gt_get = 0;
long long no_edge_but_gt_noget = 0;
long long have_edge_and_gt_noget = 0;

#endif

// isNeighbor internal-edge counter: +1 when nodeToSuper[u]==nodeToSuper[v] (count only, no per-shape breakdown)
// Always recorded (independent of RECORD_EDGE_STATS) so scripts can compute the internal-edge share.
long long internal_edge_checks = 0;

// Internal-edge query breakdown: true + false verdicts = total internal-edge queries
long long internal_edge_true_cnt = 0;
long long internal_edge_false_cnt = 0;

// Timing statistics

vector<int> globalDegreeHalf;

// ========== Coloring upper-bound prune (greedy coloring bound, Tomita MCS style) ==========
// colorBound[] reuses a static array to avoid per-level allocation (candidate size cap = 100000).
int colorBound[100000];
long long coloring_prune_count = 0;   // pruning triggered by coloring
long long coloring_call_count = 0;    // coloring invocations
// Coloring switch: !=0 color everything (all sz>1 candidate sets, no size threshold); =0 disable (nocolor mode).
int g_coloring_min_sz = 1;

void computeHalfDegree() {
    globalDegreeHalf.resize(numOriNodes);
    for (int i = 0; i < numOriNodes; i++) {
        globalDegreeHalf[i] = adjInternal[i].size() + adjExternal[i].size();
    }
}

// ========== CDA core algorithm functions ==========
// cdac queries external edges via the half adjacency; triangle counting needs no internal-edge checks
bool isNeighbor(int u, int v) {
    // Internal-edge detection
    if (nodeToSuper[u] == nodeToSuper[v]) {
        internal_edge_checks++;   // count only, no per-shape breakdown
        int SUPERU = nodeToSuper[u];
        // single guard: a single has one member, so same supernode implies u==v (self-loop); CDA has no self-loops -> false
        if (SUPERU >= firstSingleId) return false;
        auto supertype = superNodes[SUPERU].type;   // uint8_t, 1=clique 2=star 3=diamond 4=path
        bool result = false;

        if (supertype == 1) {            // clique
            result = true;
        } else if (supertype == 2) {     // star
            int center = superNodes[SUPERU].beginnode;
            if (u == center || v == center) {
                result = true;
            }
        } else if (supertype == 3) {     // diamond
            // diamond {u,v,x,y}: member0 (begin) and member1 (end) are adjacent to every member;
            // only member2-member3 are non-adjacent; i.e. adjacent iff a or b is begin/end
            int begin = superNodes[SUPERU].beginnode;
            int end = superNodes[SUPERU].endnode;
            if (u == begin || u == end || v == begin || v == end) {
                result = true;
            }
        }
        else { // path: CDA candidate construction never puts two nodes of one path together (endpoints only, see the hasKClique path branch),
               // so this branch is unreachable in practice. Unified with subac: with path_pos, single-level |delta pos|==1 check
               // (adjacent iff true edge); without it keep the original false (endpoint pairs have no direct edge).
            if (g_path_pos_ok) {
                result = queryPathPos((uint32_t)u, (uint32_t)v);
            } else {
                result = false;
            }
        }
        // Internal-edge breakdown (counted directly, independent of RECORD_EDGE_STATS)
        if (result) internal_edge_true_cnt++;
        else        internal_edge_false_cnt++;
        return result;
    }

    // External path
#if USE_GT
    // GT prune: check the GT bitset first; a common bit implies no graph edge
    bool gt_has_common = (gtCode[u] & gtCode[v]).any();

    if (gt_has_common) {
#if RECORD_EDGE_STATS
        no_edge_and_gt_get++;
#endif
        return false;
    }
    // GT found no common bit; fall through to the adjacency check
#endif

    // the half adjacency stores only v>u; binary search with u<v orientation
    bool has_edge = false;
    if (u < v)
        has_edge = binary_search(adjExternal[u].begin(), adjExternal[u].end(), v);
    else
        has_edge = binary_search(adjExternal[v].begin(), adjExternal[v].end(), u);

#if RECORD_EDGE_STATS
    if (has_edge) {
        have_edge_and_gt_noget++;
    } else {
        no_edge_but_gt_noget++;
    }
#endif
    return has_edge;
}

// greedy coloring: color candidate set st[num][0..sz-1], return the color count (upper bound).
// Colors in descending globalDegree order (high degree first gives a tighter bound). Reuses static colorBound[].
// Read-only on st; does not affect the dfsK traversal order.
int greedyColoring(int sz, int num) {
    if (sz <= 0) return 0;
    // Static index buffer (no per-level new vector), indices sorted by globalDegree descending
    static int idxBuf[100000];
    for (int i = 0; i < sz; i++) idxBuf[i] = i;
    sort(idxBuf, idxBuf + sz, [num](int a, int b) {
        return globalDegree[st[num][a]] > globalDegree[st[num][b]];
    });

    // colorBound[i] = color of the i-th (sorted) node (1-based, 0=uncolored), reuses the global array
    int maxColor = 0;
    for (int oi = 0; oi < sz; oi++) {
        int i = idxBuf[oi];
        int u = st[num][i];
        // Mark colors used by colored neighbors; the epoch trick avoids O(sz) verTag clearing.
        static int clearVer = 0;
        static int verTag[100000];
        clearVer++;
        int assigned = 0;
        for (int oj = 0; oj < oi; oj++) {
            int j = idxBuf[oj];
            if (colorBound[j] != 0 && isNeighbor(u, st[num][j])) {
                verTag[colorBound[j]] = clearVer;
            }
        }
        int c = 1;
        while (c <= maxColor && verTag[c] == clearVer) c++;
        if (c > maxColor) maxColor = c;
        colorBound[i] = c;
    }
    return maxColor;
}

bool dfsK(int sz, int num, int k) {
    // no sz==0 special case: num>=k returns early and deeper levels terminate naturally
    if (num >= k)
        return true;

    // ===== Coloring upper-bound prune =====
    // Greedily color the candidate set to get color count c (upper bound of the largest clique inside).
    // Safe prune: num + c < k means this branch cannot reach a k-clique, return directly.
    if (g_coloring_min_sz != 0 && sz > 1) {
        coloring_call_count++;
        int c = greedyColoring(sz, num);
        if (num + c < k) {
            coloring_prune_count++;
            return false;
        }
    }

    for (int i = 0; i < sz; i++) {

        if (num + (sz - i) < k) {
            return false;
        }

        int u = st[num][i];

        int cnt = 0;
        for (int j = i + 1; j < sz; j++) {
            int v = st[num][j];
            
            if (isNeighbor(u, v)) {
                st[num + 1][cnt++] = v;
            }
        }

        if (dfsK(cnt, num + 1, k)) {
            return true;
        }
    }
    return false;
}

bool solvesubK(vector<int> cliqueStart) {

    // sort ascending, consume from the back: higher global degree first
    sort(cliqueStart.begin(), cliqueStart.end(), [](int a, int b) {
        return globalDegree[a] < globalDegree[b];
    });

    int cnt;
    for (int idx = (int)cliqueStart.size() - 1; idx >= 0; idx--) {
        
        int i = cliqueStart[idx];
         
        
        if (globalDegreeHalf[i] < k - 1) {
            continue;
        }

        cnt = 0;
        for (int v : adjInternal[i]) {
            if (globalDegree[v] >= k-1) {
                st[1][cnt++] = v;
            }
        }

        for (int v : adjExternal[i]) {
            if (globalDegree[v] >= k -1) {
                st[1][cnt++] = v;
            }
        }

        if (cnt < k - 1) {
            continue;
        }

        sort(st[1], st[1] + cnt, [](int a, int b) {
            return globalDegree[a] < globalDegree[b];
        });
        // this sort fixes the candidate order inside st, inherited by deeper-level st buffers

        bool found = dfsK(cnt, 1, k);

        if (found) {
            return true;
        }
    }
    return false;
}

// tie on size broken by md descending
bool compareDescending_size_md_de(int a, int b) {
    if (superNodes[a].size != superNodes[b].size) {
        return superNodes[a].size > superNodes[b].size;
    }
    return superNodes[a].md > superNodes[b].md;
}

bool hasKClique(int k) {
    vector<int> cliqueStart;

    // Stage 1: clique region (id 0..maxCliqueNumber), no singles, synopsis always safe
    for (int fakeC = 0; fakeC < maxCliqueNumber + 1; fakeC++) {
        int C = superNodeCsMap[fakeC];

        if (superNodes[C].md < k - 1) continue;
        cliqueStart.clear();
        cliqueStart = superNodeMembers[C];

        if (solvesubK(cliqueStart)) {
            return true;
        }
    }

    // Stage 2: star/diamond/path region, bounded by min(firstSingleId, supernodenum) to exclude singles
    int nonSingleEnd = min(firstSingleId, supernodenum);
    for (int C = maxCliqueNumber + 1; C < nonSingleEnd; C++) {
        if (superNodes[C].md < k - 1) continue;

        cliqueStart.clear();
        if(superNodes[C].type==4) // path
        {
            // Upstream issue (not downstream): on CTR (adj1-verified) the synopsis begin = tail-neighbor (degree 2),
            // end = tail, so begin is not the chain head. Will be correct once contraction fixes it.
            // Downstream follows the "self-contained synopsis" design: read synopsis, never re-derive from members.
            cliqueStart.push_back(superNodes[C].beginnode);
            cliqueStart.push_back(superNodes[C].endnode);
        }
        else{
            cliqueStart = superNodeMembers[C];
        }

        if (solvesubK(cliqueStart)) {
            return true;
        }
    }

    // Stage 3: single region (firstSingleId..supernodenum-1), no synopsis, globalDegree[member] replaces md
    for (int C = firstSingleId; C < supernodenum; C++) {
        int member = superNodeMembers[C][0];              // the sole member
        if (globalDegree[member] < k - 1) continue;       // globalDegree, not md
        cliqueStart.clear();
        cliqueStart.push_back(member);
        if (solvesubK(cliqueStart)) {
            return true;
        }
    }
    return false;
}

void printCDAStats() {
#if RECORD_EDGE_STATS
    cout << "===== check edge stats =====" << "\n";
    cout << no_edge_and_gt_get << "\n";
    cout << no_edge_but_gt_noget << "\n";
    cout << have_edge_and_gt_noget << "\n";
#endif
    cout << "===== coloring bound stats =====" << "\n";
    cout << "coloring_calls: " << coloring_call_count << "\n";
    cout << "coloring_prune: " << coloring_prune_count << "\n";
    cout << "internal_edge_checks: " << internal_edge_checks << "\n";
    cout << "internal_edge_true: " << internal_edge_true_cnt << "\n";
    cout << "internal_edge_false: " << internal_edge_false_cnt << "\n";
}
vector<int> kcandi_init(string dataset_name)
{
vector<int> kcandidates;
    kcandidates = {10,20,30,40,50,60,70};
    return kcandidates;
}

// ========== Main ==========

int main(int argc, char** argv) {
    // Args: prog <dataset_name> <trial_count> [k_values...]
    //   trial_count = repetitions per k (default 1); omit k_values for the dataset default list
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <dataset_name> [trial_count] [k_values...]" << endl;
        cout << "Example: " << argv[0] << " WT 5 24 25 26 27 28 29 30" << endl;
        return 1;
    }

    string dataset_name = argv[1];

    // argv[2]: trial_count (optional, default 1); must be numeric
    int trial_count = 1;
    int k_arg_start = 2;
    if (argc >= 3) {
        string a2 = argv[2];
        bool is_num = !a2.empty();
        for (char ch : a2) { if (!isdigit((unsigned char)ch)) { is_num = false; break; } }
        if (is_num) {
            trial_count = stoi(a2);
            if (trial_count < 1) trial_count = 1;
            k_arg_start = 3;
        }
    }

    // Coloring threshold from the environment (default 256)
    if (const char* env = getenv("COLORING_MIN_SZ")) {
        g_coloring_min_sz = atoi(env);
    }
    cout << "coloring_min_sz = " << g_coloring_min_sz
         << " (set COLORING_MIN_SZ env to override; 0=disable coloring)" << endl;

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

    superNodeMembers.resize(supernodenum);
    superAdj.resize(supernodenum);

    loadMappingTxt((output_prefix + "mapping.txt").c_str());
    loadMembersTxt((output_prefix + "members.txt").c_str());   // fills superNodeMembers (order-preserving, incl. singles)

    globalDegree.clear();
    globalDegree.resize(numOriNodes);

    adjInternal.resize(numOriNodes);
    loadInternalAdjHalf((output_prefix + "internal_adj.txt").c_str());

    loadExternalAdjHalf((output_prefix + "external_adj.txt").c_str());

    // O(1) intra-path edge structure (if missing, g_path_pos_ok=false and the isNeighbor path branch stays false)
    loadPathPos(output_prefix + "path_pos.txt");

#if USE_GT
    loadBitCodesBinary(gtbin_path.c_str());

    superNodes = loadSuperGraph((output_prefix + "synopsis.txt").c_str());

    size_t gt_memory_footprint = gtCode.size() * sizeof(BitCode);
    cout << "GT data storage: " << gt_memory_footprint / (1024 * 1024) << " MB\n";
#else
    superNodes = loadSuperGraph((output_prefix + "synopsis.txt").c_str());
    cout << "GT data storage: 0 MB (USE_GT=0)\n";
#endif

    ios::sync_with_stdio(false);
    cin.tie(nullptr);

    // new
    computeHalfDegree();

    // Determine the k value list
    vector<int> kcandidates;
    if (k_arg_start < argc) {
        for (int i = k_arg_start; i < argc; i++) {
            kcandidates.push_back(stoi(argv[i]));
        }
    } else {
        // default k values (per dataset)
        kcandidates = kcandi_init(dataset_name);
        
    }

    for (int i = 0; i < kcandidates.size(); i++) {
        k = kcandidates[i];
        cout << "--------check k clique when k = " << k << "--------" << endl;

        vector<double> durations;

        for (int trial = 0; trial < trial_count; trial++) {
#if RECORD_EDGE_STATS
            no_edge_and_gt_get = 0;
            no_edge_but_gt_noget = 0;
            have_edge_and_gt_noget = 0;
#endif
            coloring_call_count = 0;
            coloring_prune_count = 0;
            internal_edge_checks = 0;
            internal_edge_true_cnt = 0;
            internal_edge_false_cnt = 0;

            auto start = std::chrono::steady_clock::now();

            superNodeCsMap.resize(supernodenum);
            maxCliqueNumber = -1;  // -1 sentinel: no clique region (all singles or supernodenum==0)
            for (int idx = 0; idx < supernodenum; idx++) {
                superNodeCsMap[idx] = idx;
                if (superNodes[idx].type != 1) {  // not clique; singles have type=0 (empty) and always break here
                    maxCliqueNumber = idx - 1;
                    break;
                }
                maxCliqueNumber = idx;  // all cliques up to the end
            }

            int maxcliquesize = 0;
            if (maxCliqueNumber >= 0) {
                sort(superNodeCsMap.begin(), superNodeCsMap.begin() + maxCliqueNumber + 1, compareDescending_size_md_de);
                maxcliquesize = superNodes[superNodeCsMap[0]].size;  // [0] is guaranteed clique
                cout << "max contracted clique size = " << maxcliquesize << endl;
                cout << "max contracted clique C = " << superNodeCsMap[0] << endl;
            } else {
                cout << "no clique supernode (maxCliqueNumber = -1)" << endl;
            }

            if (maxCliqueNumber >= 0 && maxcliquesize >= k
                && superNodes[superNodeCsMap[0]].type == 1) {  // clique
                cout << "yes for the " << k << " clique!" << "max contracted clique size = " << maxcliquesize << endl;
            } else {
                if (hasKClique(k)) {
                    cout << "yes " << k << " clique" << endl;
                } else {
                    cout << "no " << k << " clique" << endl;
                }
            }

            auto end = std::chrono::steady_clock::now();
            std::chrono::duration<double> duration = end - start;
            durations.push_back(duration.count());
        }

        double avg_duration = std::accumulate(durations.begin(), durations.end(), 0.0) / durations.size();
        cout << fixed << setprecision(6) << "Average time taken: " << avg_duration << " seconds\n";
        cout << avg_duration << "\n";

        printCDAStats();
    }

    return 0;
}
