#pragma GCC optimize(3,"inline")
#include "turboiso.h"
#include "graph.h"
#include <iostream>
#include <vector>
#include <unordered_map>
#include <unordered_set>
#include <string>
#include <algorithm>
#include <functional>
#include <fstream>
#include <sstream>
#include <bits/stdc++.h>
#include <csignal>
#include <unistd.h>

using namespace std;

// ========== GT encoding switches ==========
// DISABLE_GT: disable GT; has_edge_in_data falls back to binary search
// NO_GT_STATS: do not record GT counters (excludes counter-increment overhead)
// #define DISABLE_GT
// #define NO_GT_STATS

// ========== Globals (algorithm-specific; graph globals live in ../common/graph.cpp) ==========

int exploreCR_call_count = 0;

// Supergraph structure
vector<int> super_order;  // sorted supernode indices (primary: type rank, secondary: md)

// Labels and degrees
vector<string> dataNodeLabel;

// GT hit metrics
long long no_edge_and_gt_get = 0;
long long no_edge_but_gt_noget = 0;
long long have_edge_and_gt_noget = 0;

// Internal-edge query breakdown: true + false verdicts = total internal-edge queries
long long internal_edge_true_cnt = 0;
long long internal_edge_false_cnt = 0;

// Match counters
long long matchtarget = -1;
long long current_match_cnt = 0;

// ===== Timeout control (wall-clock seconds); 0 = unlimited =====
long long g_timeout_sec = 300;
std::chrono::steady_clock::time_point g_subac_t0;
bool g_timed_out = false;

// visited epoch: avoids full visited.assign reset (root cause of the CTR stall)
// visited[v] == current_epoch means visited; current_epoch++ per start_data_vertex loop invalidates old values
int g_current_epoch = 0;
int g_f_epoch = 0;  // F epoch (avoids full F.assign reset)

int pernumk = -1;  // q1 clique permutation count (3! = 6 for a 4-clique)

// current query id (set in main, read by runMatch, for query-specific logic such as the q2 manual order)
int g_query_id = -1;

// ========== Fixed NEC match order per query graph ==========
// Dumped from generateRIOrder_NEC on BK (q2 was manually overridden at the time):
//   q1: 0 1          NEC0{0} → NEC1{1,2,3}
//   q2: 0 2 1        NEC0{0} → NEC2{2} → NEC1{1,3}
//   q3: 0 2 1 3 4    NEC0{0} → NEC2{2} → NEC1{1} → NEC3{4} → NEC4{3}
//   q4: 0 3 2 1      NEC0{1} (root = query vertex 1) -> NEC3{3,4} -> NEC2{2} -> NEC1{0}
//   q5: 0 2 3 4 1 5  all single-node NECs
//   q6: 0 1 3 2 4    NEC0{0} → NEC1{1,3} → NEC3{4} → NEC2{2} → NEC4{5}
// The NEC structure and order depend only on the query graph topology, not on the data graph, hence hardcoded;
// generateRIOrder_NEC is kept but not called; restore it to recompute.
static const std::map<int, std::vector<int>> g_fixed_nec_order = {
    {1, {0, 1}},
    {2, {0, 2, 1}},
    {3, {0, 2, 1, 3, 4}},
    {4, {0, 3, 2, 1}},
    {5, {0, 2, 3, 4, 1, 5}},
    {6, {0, 1, 3, 2, 4}},
};

// DFS performance globals
long long dfs_total_calls = 0;
long long dfs_total_time_all_ns = 0;
long long has_edge_in_data_total_calls = 0;

// ExploreCR performance
double explorecr_time_ms = 0.0;
long long explorecr_calls = 0;

// ===== SIGALRM soft timeout: on fire, print phase times and exit (wherever we are stuck) =====
void alarm_handler(int sig) {
    (void)sig;
    double elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - g_subac_t0).count();
    std::cout << "\n========== ALARM TIMEOUT ==========" << std::endl;
    std::cout << "elapsed: " << elapsed << "s" << std::endl;
    std::cout << "ExploreCR total time: " << (explorecr_time_ms / 1000.0) << " s" << std::endl;
    std::cout << "DFS (subgraphSearch) total time: " << (dfs_total_time_all_ns / 1e9) << " s" << std::endl;
    std::cout << "current_match_cnt: " << current_match_cnt << std::endl;
    std::cout << "ExploreCR outer calls: " << exploreCR_call_count << std::endl;
    std::cout << "ExploreCR recursive calls: " << explorecr_calls << std::endl;
    std::cout << "==================================" << std::endl;
    std::cout.flush();
    _exit(0);
}

double comb(int n, int k)
{
    if (n < k) return 0;
    double r = 1;
    for (int i = 0; i < k; i++)
        r = r * (n - i) / (i + 1);
    return r;
}

// Permutation count A(n, k) = n! / (n-k)!
long long pernum_A_M_N(int n, int k)
{
    if (n < k || k < 0) return 0;
    if (k == 0) return 1;

    long long result = 1;
    for (int i = 0; i < k; i++) {
        result *= (n - i);
    }
    return result;
}

// =============== TurboISO match-order functions (based on main.cpp) ===============

// Comparator for Elem (for sorting)
int cmpElem(const void *a, const void *b)
{
    if(fabs(((Elem *)a)->value - ((Elem *)b)->value) > 1e-6)
    {
        if(((Elem *)a)->value - ((Elem *)b)->value > 1e-6)
        {
            return 1;
        }
        else return -1;
    }
    else return ((Elem *)a)->v - ((Elem *)b)->v;
}

// Recursive match-order function (based on main.cpp)
// Parameters:
// - nec: current NEC node
// - CR: candidate region tree
// - qadj: query graph adjacency list
// - order: output order array
// - product: accumulated product factor
// - all_necs: all NEC nodes (for bounds checks)
double determineMatchOrder(
        NECNode* nec,
        CRTree* CR,
        const std::vector<std::vector<int>>& qadj,
        Elem* order,
        int product,
        const std::vector<NECNode*>& all_necs)
{
    // Edge count ENum (excluding intra-NEC edges)
    int ENum = 0;
    int nSize = nec->vertex_set.size();
    for (int ii = 0; ii < nSize; ii++)
    {
        int vv = nec->vertex_set[ii];
        int graS = qadj[vv].size();
        for (int i = 0; i < graS; i++)
            ENum += 1;
    }

    // For |NEC| > 1, subtract the intra-NEC edge count
    if (nSize > 1)
    {
        // Check whether the NEC has an internal edge
        int vv0 = nec->vertex_set[0];
        int vv1 = nec->vertex_set[1];
        for (int nb : qadj[vv0])
        {
            if (nb == vv1)
            {
                // internal edge present: subtract nSize*(nSize-1)/2
                ENum -= nSize * (nSize - 1) / 2;
                break;
            }
        }
    }

    // With children, recurse
    if (!nec->children.empty())
    {
        int Product;
        if (nec->parent == nullptr) // root
        {
            int eNum = 0;
            for (NECNode* child : nec->children)
            {
                eNum += nSize * child->vertex_set.size();
            }
            Product = product * (ENum - eNum + 1);
        }
        else
        {
            int eNum = nSize * nec->parent->vertex_set.size();
            for (NECNode* child : nec->children)
            {
                eNum += nSize * child->vertex_set.size();
            }
            Product = product * (ENum - eNum + 1);
        }

        // Recursively score all children, take the minimum
        double Min = std::numeric_limits<double>::max();
        for (NECNode* child : nec->children)
        {
            double tempD = determineMatchOrder(child, CR, qadj, order, Product, all_necs);
            if (Min - tempD > 1e-6)
                Min = tempD;
        }
        order[nec->id].v = nec->id;
        order[nec->id].value = Min;
        return order[nec->id].value;
    }
    else // leaf
    {
        int eNum = nSize * nec->parent->vertex_set.size();
        int Product = product * (ENum - eNum + 1);
        int nec_id = nec->id;

        if (nSize == 1)
        {
            // |NEC| == 1: total candidate count / Product
            int Num = 0;
            if (CR && CR->CR && nec_id >= 0 && nec_id < (int)all_necs.size())
            {
                auto &entries = CR->CR[nec_id];
                for (auto &pool : entries)
                    Num += pool.size();
            }
            order[nec_id].v = nec_id;
            order[nec_id].value = (double)Num / Product;

        }
        else
        {
            // |NEC| > 1: sum of combination counts / Product
            double Num = 0.0;
            if (CR && CR->CR && nec_id >= 0 && nec_id < (int)all_necs.size())
            {
                auto &entries = CR->CR[nec_id];
                for (auto &pool : entries)
                    Num += comb(pool.size(), nSize);
            }
            order[nec_id].v = nec_id;
            order[nec_id].value = Num / Product;

        }
        return order[nec_id].value;
    }
}

// Wrapper: invokes the main.cpp-style match-order function

void generateRIOrder_NEC(
    const std::vector<NECNode*>& all_necs,
    const std::vector<std::vector<int>>& qadj,
    std::vector<int>& order)
{
    int n = (int)all_necs.size();
    order.clear();
    if (n == 0) return;

    std::vector<bool> visited(n, false);

    // ---------- Step 1: Start vertex ----------
    int start = 0;
    order.push_back(start);
    visited[start] = true;

    // ---------- Step 2: Greedy expansion ----------
    // Each step: pick the unvisited NEC with the most backward neighbors
    for (int i = 1; i < n; i++) {
        int max_bn = -1, best = -1;
        int best_deg = -1;

        for (int u = 0; u < n; u++) {
            if (visited[u]) continue;

            int bn = 0;
            for (int q : all_necs[u]->vertex_set) {
                for (int qnbr : qadj[q]) {
                    for (int j = 0; j < i; j++) {
                        const auto& oset = all_necs[order[j]]->vertex_set;
                        if (std::find(oset.begin(), oset.end(), qnbr) != oset.end()) {
                            bn++;
                            goto next_q_nec;
                        }
                    }
                }
                next_q_nec:;
            }

            int deg_sum = 0;
            for (int q : all_necs[u]->vertex_set)
                deg_sum += (int)qadj[q].size();

            if (bn > max_bn || (bn == max_bn && deg_sum > best_deg)) {
                max_bn = bn;
                best = u;
                best_deg = deg_sum;
            }
        }

        order.push_back(best);
        visited[best] = true;
    }
}

void determineMatchOrder_MainCPP(
        NECNode* root,
        CRTree* CR,
        std::vector<int>& order,
        const std::vector<NECNode*>& all_necs,
        const QueryGraph& Q,
        const std::vector<std::vector<int>>& qadj,
        const std::vector<NECNode*>& node2nec)
{
    order.clear();

    // Allocate the order array
    Elem* order_arr = new Elem[all_necs.size()];

    // Call the recursive function (from the root, product=1)
    determineMatchOrder(root, CR, qadj, order_arr, 1, all_necs);

    // Sort by score ascending
    qsort(order_arr, all_necs.size(), sizeof(Elem), cmpElem);

    // Emit into the order vector
    for (size_t i = 0; i < all_necs.size(); i++)
    {
        order.push_back(order_arr[i].v);
        int nec_id = order_arr[i].v;
        NECNode* nec = all_necs[nec_id];
        int nSize = nec->vertex_set.size();
        double score = order_arr[i].value;

        // Compute Num and Product (debug)
        if (nSize == 1) {
            int Num = 0;
            if (CR && CR->CR && nec_id >= 0 && nec_id < (int)all_necs.size()) {
                auto &entries = CR->CR[nec_id];
                for (auto &pool : entries)
                    Num += pool.size();
            }
            // Back out Product: score = Num / Product -> Product = Num / score
            double Product = (score > 0) ? Num / score : 0;
        } else {
            double Num = 0.0;
            if (CR && CR->CR && nec_id >= 0 && nec_id < (int)all_necs.size()) {
                auto &entries = CR->CR[nec_id];
                for (auto &pool : entries)
                    Num += comb(pool.size(), nSize);
            }
            double Product = (score > 0) ? Num / score : 0;
        }

    }

    delete[] order_arr;
}

inline bool __attribute__((always_inline)) has_edge_in_data(int u, int v) {
#ifndef NO_GT_STATS
    has_edge_in_data_total_calls++;
#endif

    if(nodeToSuper[u]==nodeToSuper[v])
    {
        // separate timing for internal edges
        // TIMER_START(internal_edge);
        int SUPERU = nodeToSuper[u];
        // single guard: a single supernode has one member, so same supernode implies u==v (self-loop).
        // No synopsis to read; subgraph isomorphism has no self-loops -> return false.
        if (SUPERU >= firstSingleId) {
            internal_edge_false_cnt++;   // single self-loop: non-edge
            return false;
        }
        // Per-shape handling (type as uint8_t: 1=clique 2=star 3=diamond 4=path 5=single)
        auto supertype = superNodes[SUPERU].type;

        // Only path needs the adjInternal verification (clique/star/diamond internal adjacency follows from shape rules)
        // Contraction does not guarantee consecutive path nodes are truly adjacent in the original graph (58% false-edge rate measured on CTR).
        // The clique and star shape rules are verified reliable.
        if(supertype==1)  // clique
        {
            internal_edge_true_cnt++;    // all clique internal pairs adjacent
            return true;  // clique: all internal pairs adjacent
        }
        else if(supertype==2)  // star
        {
            int center = superNodes[SUPERU].beginnode;
            if(u==center || v==center) {
                internal_edge_true_cnt++;    // star center-leaf
                return true;  // star: center-leaf adjacent
            }
            internal_edge_false_cnt++;       // star leaf-leaf non-edge
            return false;      // star: leaves mutually non-adjacent
        }
        else if(supertype==3)  // diamond
        {
            // diamond {u,v,x,y}: member0 (begin) and member1 (end) are adjacent to every member;
            // only member2-member3 are non-adjacent; i.e. adjacent iff u or v is begin/end
            int begin = superNodes[SUPERU].beginnode;
            int end = superNodes[SUPERU].endnode;
            if(u==begin || u==end || v==begin || v==end) {
                internal_edge_true_cnt++;    // diamond involves begin/end
                return true;
            }
            internal_edge_false_cnt++;       // diamond x-y non-edge
            return false;      // diamond: member2-member3 (x-y) non-edge
        }
        else // path: single-level check via pos, |delta pos|==1 (adjacent iff true edge; 0% false edges on BK/CTR/USA)
        {
            if (g_path_pos_ok) {
                if (queryPathPos((uint32_t)u, (uint32_t)v)) {
                    internal_edge_true_cnt++;    // path adjacent pair: true edge
                    return true;
                }
                internal_edge_false_cnt++;
                return false;
            }
            // Fallback: without path_pos.txt use the original two-level O(N) logic
            const vector<int>& pathNodes = superNodeMembers[SUPERU];
            for(size_t i = 0; i < pathNodes.size() - 1; i++)
            {
                if((pathNodes[i] == u && pathNodes[i+1] == v) ||
                   (pathNodes[i] == v && pathNodes[i+1] == u))
                {
                    if (binary_search(adjInternal[u].begin(), adjInternal[u].end(), v)) {
                        internal_edge_true_cnt++;    // path adjacent pair: true edge
                        return true;
                    }
                    internal_edge_false_cnt++;       // path adjacent pair: false edge
                    return false;
                }
            }
            internal_edge_false_cnt++;           // path non-adjacent pair
            return false;
        }
    }

    // ========== External edges: GT prune + binary search ==========
#ifndef DISABLE_GT
    bool gt_has_common = (gtCode[u] & gtCode[v]).any();
    if(gt_has_common)
    {
#ifndef NO_GT_STATS
        no_edge_and_gt_get++;   // GT correctly predicts non-edge, skipping binary search
#endif
        return false;
    }
#endif
    // GT disabled or missed -> binary search
    {
        // half adjacency list unusable here; check degrees directly
        if(adjExternal[u].size()>adjExternal[v].size())   swap(u, v);

        bool has_edge = binary_search(adjExternal[u].begin(), adjExternal[u].end(), v);

        if (has_edge)
        {
#if !defined(DISABLE_GT) && !defined(NO_GT_STATS)
            have_edge_and_gt_noget++;  // GT miss (edge exists, GT did not predict it)
#endif
            return true;
        }
        else
        {
#if !defined(DISABLE_GT) && !defined(NO_GT_STATS)
            no_edge_but_gt_noget++;    // non-edge and GT missed
#endif
            return false;
        }
    }
}

/////////////////////// Helper: QueryGraph -> adjacency lists ///////////////////////
void buildQueryAdj(const QueryGraph &Q, std::vector<std::vector<int>> &qadj) {
    int qn = (int)Q.node_ids.size();
    qadj.assign(qn, {});
    for (auto &e : Q.edges) {
        int a = e.first;
        int b = e.second;
        // assume edges are given as node indices consistent with Q.node_ids
        // If Q.node_ids are arbitrary labels, we assume edges already use those indices.
        qadj[a].push_back(b);
        qadj[b].push_back(a);
    }
    // remove duplicates and normalize
    for (int i = 0; i < qn; ++i) {
        sort(qadj[i].begin(), qadj[i].end());
        qadj[i].erase(unique(qadj[i].begin(), qadj[i].end()), qadj[i].end());
    }
}

// new
// Rewritten from the FindNEC logic in main.cpp
static void FindNEC(vector <vector <int> > *NECV, vector <int> *vertexlist, 
                   const QueryGraph &Q, const std::vector<std::vector<int>> &qadj) 
{
    int Size = vertexlist->size();
    if(Size <= 1) {
        if(Size == 1) {
            (*NECV).push_back(*vertexlist);
        }
        return;
    }

    // Equivalence check logic from main.cpp
    auto equals = [&](const std::vector<int>& v1_neighbors, const std::vector<int>& v2_neighbors) -> bool {
        if(v1_neighbors.size() != v2_neighbors.size()) {
            return false;
        }
        return std::equal(v1_neighbors.begin(), v1_neighbors.end(), v2_neighbors.begin());
    };

    //NOTICE: the first class, there does not exist edges between equivalent query nodes.
    int *p = new int[Size];
    for(int i = 0; i < Size; i++) p[i] = i;

    //union set
    for(int i = 1; i < Size; i++) {
        int flag = i;
        for(int j = 0; j < i; j++) {
            if(equals(qadj[(*vertexlist)[i]], qadj[(*vertexlist)[j]])) {
                flag = j;
                break;
            }
        }
        p[i] = p[flag];
        p[flag] = i;
    }
    
    for(int i = 0; i < Size; i++) {
        if(p[i] > i) {
            vector <int> vlist;
            vlist.push_back((*vertexlist)[i]);
            int Pos = p[i];
            while(Pos != i) {
                vlist.push_back((*vertexlist)[Pos]);
                Pos = p[Pos];
            }
            (*NECV).push_back(vlist);
        }
    }
    
    for(int i = Size - 1; i >= 0; i--) {
        if(p[i] != i) {
            vector <int>::iterator Iter = vertexlist->begin() + i;
            (*vertexlist).erase(Iter);
        }
    }
    
    if(p != NULL)
        delete []p;

    Size = vertexlist->size();
    if(Size <= 1) {
        if(Size == 1) {
            (*NECV).push_back(*vertexlist);
        }
        return;
    }

    //NOTICE: the second class, there are edges between equivalent query nodes.
    //These NECs can not be found in the above process, e.g., v1-v2, they have different neighbors.
    // Second-class NEC handling from main.cpp
    if (vertexlist->empty()) return;
    
    
    for(int i = 0; i < vertexlist->size(); ++i) {
        if((*vertexlist)[i] == -1) {
            continue;
        }
        
        vector <int> a;  // current NEC group
        vector <int> b;  // corresponding indices
        int Vi = (*vertexlist)[i];
        a.push_back(Vi);
        b.push_back(i);
        int pos2 = -1;
        for (int k = 0; k < qadj[Vi].size(); k++) {
            int neighbor = qadj[Vi][k];
            bool in_group = false;
            for (int vtx : *vertexlist) {
                if (vtx == neighbor) {
                    in_group = true;
                    break;
                }
            }
            if (in_group) {
                pos2 = k;
                break;
            }
        }
        
        if(pos2 == -1) { //Vi has no neighbor in this group
            (*NECV).push_back(a);
            (*vertexlist)[i] = -1;
            continue;
        }
        for(int j = i + 1; j < vertexlist->size(); j++) {
            int Vj = (*vertexlist)[j];
            if(Vj == -1) {
                continue;
            }
            bool connected_to_Vi = false;
            for (int neighbor : qadj[Vj]) {
                if (neighbor == Vi) {
                    connected_to_Vi = true;
                    break;
                }
            }
            
            if (!connected_to_Vi) continue;
            int sizea = a.size();
            bool fmm = true;
            for(int mm = 0; mm < sizea; mm++) {
                bool connected = false;
                for (int neighbor : qadj[Vj]) {
                    if (neighbor == a[mm]) {
                        connected = true;
                        break;
                    }
                }
                if (!connected) {
                    fmm = false;
                    break;
                }
            }
            
            if(fmm) {
                auto exclusive_equals = [&](int u, int v) -> bool {
                    const std::vector<int>& Nu = qadj[u];
                    const std::vector<int>& Nv = qadj[v];
                    std::vector<int> Nu_minus_v, Nv_minus_u;
                    for (int n : Nu) {
                        if (n != v) Nu_minus_v.push_back(n);
                    }
                    for (int n : Nv) {
                        if (n != u) Nv_minus_u.push_back(n);
                    }
                    
                    if (Nu_minus_v.size() != Nv_minus_u.size()) return false;
                    std::sort(Nu_minus_v.begin(), Nu_minus_v.end());
                    std::sort(Nv_minus_u.begin(), Nv_minus_u.end());
                    return Nu_minus_v == Nv_minus_u;
                };
                
                bool success = exclusive_equals(Vi, Vj);
                if(success) {
                    a.push_back(Vj);
                    b.push_back(j);
                }
            }
        }
        (*NECV).push_back(a);
        int sizea = a.size();
        for(int nn = sizea - 1; nn >= 0; nn--) {
            (*vertexlist)[b[nn]] = -1;
        }
    }
    vertexlist->erase(
        std::remove_if(vertexlist->begin(), vertexlist->end(), 
                      [](int x) { return x == -1; }), 
        vertexlist->end()
    );
}

// Rewritten from RewriteToNECTree in main.cpp, including level bookkeeping
    // Level index table (start_idx/end_idx arrays)
static NECNode* buildNECTree(const QueryGraph &Q, int start_q, const std::vector<std::vector<int>> &qadj, 
                             std::vector<NECNode*> &all_necs, std::vector<NECNode*> &node2nec_out) {
    int qn = (int)Q.node_ids.size();
    std::vector<bool> flag(qn, false);
    NECNode* root = new NECNode();
    root->id = 0;
    root->vertex_set.push_back(start_q);
    root->start_idx = 0;
    root->end_idx = 0;
    root->parent = nullptr;
    
    all_necs.push_back(root);
    node2nec_out.assign(qn, nullptr);
    node2nec_out[start_q] = root;
    flag[start_q] = true;

    int currentS = -1;
    int currentE = -1;
    int nextS = 0;
    int nextE = 0;
    int childS = 0;
    int childE = 0;
    
    // Level index table: per-level NEC node ranges
    std::vector<std::pair<int, int>> level_index; // {start_index, end_index}
    level_index.push_back({0, 0}); // root level
    
    int current_level = 0;
    
    while(nextE >= nextS) {
        currentS = nextS;
        currentE = nextE;
        nextS = currentE + 1;
        nextE = currentE;
        childS = currentE + 1;
        childE = currentE;
        if (current_level < (int)level_index.size()) {
            level_index[current_level] = {currentS, currentE};
        } else {
            level_index.push_back({currentS, currentE});
        }
        
        //i is the NEC node ID
        for(int i = currentS; i <= currentE; i++) {
            // Emulates the C variable in main.cpp (labelVlist type)
            std::vector<std::pair<std::string, std::vector<int>>> C; // {label, vertices}
            
            int Size2 = all_necs[i]->vertex_set.size();
            for(int j = 0; j < Size2; j++) {
                int v = all_necs[i]->vertex_set[j];
                int Size3 = qadj[v].size();
                for(int k = 0; k < Size3; k++) {
                    int neighbor = qadj[v][k];
                    if(!flag[neighbor]) {
                        // labels uniform "user" (originally from Q.node_labels, disabled)
                        std::string label_str = "user";
                        int pos = -1;
                        for(int c_idx = 0; c_idx < (int)C.size(); c_idx++) {
                            if(C[c_idx].first == label_str) {
                                pos = c_idx;
                                break;
                            }
                        }
                        
                        if(pos == -1) {
                            std::pair<std::string, std::vector<int>> new_label_group;
                            new_label_group.first = label_str;
                            new_label_group.second.push_back(neighbor);
                            flag[neighbor] = true;
                            C.push_back(new_label_group);
                        } else {
                            bool found = false;
                            for(int vtx : C[pos].second) {
                                if(vtx == neighbor) {
                                    found = true;
                                    break;
                                }
                            }
                            if(!found) {
                                flag[neighbor] = true;
                                C[pos].second.push_back(neighbor);
                            }
                        }
                    }
                }
            }
            
            if(!C.empty()) {
                int Size3 = C.size();
                for(int j = 0; j < Size3; j++) {
                    vector <vector <int> > NECV;
                    FindNEC(&NECV, &(C[j].second), Q, qadj);

                    int Size4 = NECV.size();
                    nextE += Size4;
                    childE += Size4;
                    if(all_necs[i]->children.empty()) {
                        all_necs[i]->children.resize(Size4);
                    } else {
                        size_t old_size = all_necs[i]->children.size();
                        all_necs[i]->children.resize(old_size + Size4);
                    }
                    
                    for(int k = 0; k < Size4; k++) {
                        NECNode* new_node = new NECNode();
                        new_node->id = all_necs.size();
                        new_node->vertex_set = NECV[k];
                        new_node->start_idx = 0;
                        new_node->end_idx = NECV[k].size() - 1;
                        new_node->parent = all_necs[i];
                        
                        all_necs.push_back(new_node);
                        all_necs[i]->children[all_necs[i]->children.size() - Size4 + k] = new_node;
                        for(int vertex : NECV[k]) {
                            node2nec_out[vertex] = new_node;
                        }
                    }
                }
            }
        }
        
        current_level++;
    }
    
    // Optional: print level info for debugging
    /*
    std::cout << "=== NEC Tree Levels ===" << std::endl;
    for (int i = 0; i < (int)level_index.size(); i++) {
        std::cout << "Level " << i << ": nodes " << level_index[i].first 
                  << " to " << level_index[i].second << std::endl;
    }
    */
    
    return root;
}

void PureInsert(std::vector<int> &vec, int x) {
    if (std::find(vec.begin(), vec.end(), x) == vec.end())
        vec.push_back(x);
}

static void ClearCR(int u, int vp, CRTree *CR) {
    auto &parents = CR->parent[u];
    for (int i = 0; i < (int)parents.size(); i++) {
        if (parents[i] == vp) {
            if (i < (int)CR->CR[u].size()) CR->CR[u].erase(CR->CR[u].begin() + i);
            parents.erase(parents.begin() + i);
            return;
        }
    }
}

struct Neighbor {
    int uc_prime;
    int pos;
    int NeighborN;
};
bool ExploreCR(int u_prime,
               std::vector<int> *VM,
               CRTree *CR,
               int v,  // parent data node (-1 for root)
               std::vector<NECNode*> &all_necs,
               const std::vector<std::vector<int>> &qadj,
               const QueryGraph &Q,
               std::vector<int> &visited)
{
    // ---------------------------------------------
    // ---------------------------------------------
    explorecr_calls++;

    // Timeout check: poll the clock and set g_timed_out (so the outer loop is not stuck waiting on ExploreCR)
    if (g_timeout_sec > 0 && g_timed_out == false) {
        if (std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
            g_timed_out = true;
        }
    }
    if (g_timed_out) return false;  // timed out, unwind fast

    int q_rep = all_necs[u_prime]->vertex_set[0];
    int q_features = qadj[q_rep].size();
    // VM entries are already filtered by supernode label/degree upstream; process directly
    for (int v_prime : *VM) {
        // Timeout check (top of the main loop, also exits when VM is huge)
        if (g_timed_out) return false;
        if (visited[v_prime] == g_current_epoch || globalDegree[v_prime] < q_features) continue;

        // Prune 2 (supernode md) removed: empty single synopsis made the condition vacuously true, wrongly pruning all single candidates (fixed 0809)
        // Prune 3: topological propagation (after generateCandidates from the survey).
        // v_prime must have a neighbor with degree >= the child NEC degree requirement,
        // otherwise that child cannot even find a first candidate and v_prime cannot extend -> early stop.
        // Valid without labels (degree + adjacency only).
        if (!all_necs[u_prime]->children.empty()) {
            bool topo_ok = true;
            for (auto *child : all_necs[u_prime]->children) {
                int q_child = child->vertex_set[0];
                int child_deg = (int)qadj[q_child].size();
                bool has_valid_neighbor = false;
                const auto &nbrs = adjMerged[v_prime];
                for (int n : nbrs) {
                    if (globalDegree[n] >= child_deg) {
                        has_valid_neighbor = true;
                        break;
                    }
                }
                if (!has_valid_neighbor) { topo_ok = false; break; }
            }
            if (!topo_ok) continue;
        }

        // // fast degree prune

        visited[v_prime] = g_current_epoch;
        bool matched_children = true;
        // ---------------------------------------------
        // If the NEC has children, build the neighbor candidate sets
        // ---------------------------------------------
        if (!all_necs[u_prime]->children.empty())
        {
            std::vector<std::pair<int, std::vector<int>>> childPools;
            childPools.reserve(all_necs[u_prime]->children.size());

            for (auto *child : all_necs[u_prime]->children)
            {
                int q_child = child->vertex_set[0];
                const std::vector<int>& cand = adjMerged[v_prime];

                // Paper logic: keep every child pool (even empty)
                // Paper line 7: EXPLORECR(u'_c, adj(v', L(u'_c)), CR)
                // Pass adj directly without an emptiness check
                childPools.emplace_back(child->id, cand);
            }

            if (!matched_children) {
                visited[v_prime] = 0; /* backtrack restore: unmark within the same epoch */
                continue;
            }

            // -------- sort children by candidate pool size (smallest first)
            std::vector<std::pair<int, std::vector<int>>> nonEmptyPools;
            for (auto &p : childPools) {
                if (!p.second.empty()) {
                    nonEmptyPools.push_back(p);
                }
            }

            // All pools empty: skip this v_prime
            if (nonEmptyPools.empty()) {
                visited[v_prime] = 0; /* backtrack restore: unmark within the same epoch */
                continue;
            }
            std::sort(nonEmptyPools.begin(), nonEmptyPools.end(),
                      [](auto &a, auto &b) { return a.second.size() < b.second.size(); });

            // -------- recursively explore each child (paper Algorithm 3 line 7)
            for (size_t idx = 0; idx < nonEmptyPools.size(); idx++)
            {
                // Timeout check before recursing into a child (avoid being stuck deep)
                if (g_timed_out) { matched_children = false; break; }
                int childNEC = nonEmptyPools[idx].first;
                auto &childVM = nonEmptyPools[idx].second;

                // Paper Algorithm 2 line 7: EXPLORECR(u'_c, adj(v', L(u'_c)), CR)
                if (!ExploreCR(childNEC, &childVM, CR, v_prime, all_necs, qadj, Q, visited))
                {
                    for (size_t k = 0; k < idx; k++) {
                        ClearCR(nonEmptyPools[k].first, v_prime, CR);
                    }
                    // Paper lines 10-11: matched := false; break;
                    matched_children = false;
                    break;
                }
            }

            if (!matched_children) {
                visited[v_prime] = 0; /* backtrack restore: unmark within the same epoch */
                continue;
            }
        }

        visited[v_prime] = 0; /* backtrack restore: unmark within the same epoch */

        // ---------------------------------------------
        // ---------------------------------------------
        int pos = -1;
        for (int i = 0; i < (int)CR->parent[u_prime].size(); i++) {
            if (CR->parent[u_prime][i] == v) {
                pos = i;
                break;
            }
        }

        if (pos == -1) {   // new parent entry
            CR->parent[u_prime].push_back(v);
            CR->CR[u_prime].push_back({ v_prime });
        } else {
            PureInsert(CR->CR[u_prime][pos], v_prime);
        }

        // ---------------------------------------------
        // ---------------------------------------------
        if (v == -1) {
            int pos = -1;
            for (int i = 0; i < (int)CR->parent[u_prime].size(); i++) {
                if (CR->parent[u_prime][i] == -1) {
                    pos = i;
                    break;
                }
            }
            if (pos != -1 && (int)CR->CR[u_prime][pos].size() >= (int)all_necs[u_prime]->vertex_set.size())
            {
                return true;  // paper Algorithm 3 line 19: return DONE
            }
        }
    } // end for v_prime in *VM

    // ---------------------------------------------
    // ---------------------------------------------
    if (v != -1) {
        int pos = -1;  
        for (int i = 0; i < (int)CR->parent[u_prime].size(); i++)
            if (CR->parent[u_prime][i] == v)
                pos = i;

        if (pos == -1) return false;
        if ((int)CR->CR[u_prime][pos].size() < (int)all_necs[u_prime]->vertex_set.size())
        {
            ClearCR(u_prime, v, CR);
            return false;
        }
    }
    
    // root NEC (v=-1) returns true by default
    // Paper Algorithm 3 line 19: return DONE
    return true;
}

inline bool __attribute__((always_inline)) isJoinable_usinghas_edge_in_data(
    int d,
    const std::vector<int>& neighbors_to_check)
{
    auto start = std::chrono::steady_clock::now();
    for (int dn : neighbors_to_check) {
        if (!has_edge_in_data(d, dn)) {
            auto end = std::chrono::steady_clock::now();
            auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
            return false;
        }
    }
    auto end = std::chrono::steady_clock::now();
    auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return true;
}
// Efficient combination iterator - similar to std::next_permutation

struct ComboIterator {
    const std::vector<int>& pool;
    int k;
    std::vector<int> indices;
    bool finished;
    bool needClique;
    mutable std::vector<int> combo_buffer;

    ComboIterator(const std::vector<int>& p, int k_, bool clique)
        : pool(p), k(k_), indices(k_), finished(false), needClique(clique), combo_buffer(k_) {
        if (k > (int)pool.size()) {
            finished = true;
            return;
        }
        for (int i = 0; i < k; ++i) {
            indices[i] = i;
        }
        if (needClique && !checkClique()) {
            next();
        }
    }

    bool checkClique() const {
        for (int i = 0; i < k; ++i) {
            for (int j = i + 1; j < k; ++j) {
                int u = pool[indices[i]];
                int v = pool[indices[j]];
                if (!has_edge_in_data(u, v)) {
                    return false;
                }
            }
        }
        return true;
    }

    const std::vector<int>& getCombo() const {
        for (int i = 0; i < k; ++i) {
            combo_buffer[i] = pool[indices[i]];
        }
        return combo_buffer;
    }

    bool next() {
        if (finished) {
            return false;
        }

        int n = (int)pool.size();

        do {
            int i = k - 1;
            while (i >= 0 && indices[i] == n - k + i) {
                i--;
            }

            if (i < 0) {
                finished = true;
                return false;
            }

            indices[i]++;

            for (int j = i + 1; j < k; ++j) {
                indices[j] = indices[j - 1] + 1;
            }

            if (!needClique || checkClique()) {
                return true;
            }

        } while (!finished);
        return false;
    }
};

/**
 * @brief Helper: binomial coefficient C(n, k)
 */

// UpdateState and RestoreState functions (similar to main.cpp)
void UpdateState(std::vector<int> &M, std::vector<int> &F,
                        const std::vector<int> &qV, const std::vector<int> &gV) {
    // qV: query vertex indices (e.g., qGroup)
    // gV: data node ids corresponding to qV positions (e.g., perm)
    for (size_t i = 0; i < qV.size(); ++i) {
        int qid = qV[i];
        int did = gV[i]; // data node id
        M[qid] = did;
            F[did] = g_f_epoch;
    }
}

void RestoreState(std::vector<int> &M, std::vector<int> &F,
                         const std::vector<int> &qV, const std::vector<int> &gV) {
    for (size_t i = 0; i < qV.size(); ++i) {
        int qid = qV[i];
        int did = gV[i];
        M[qid] = -1;
            F[did] = 0;
    }
}

void subgraphSearch_TurboIso_rec(
    const std::vector<int> &order,
    const std::vector<std::vector<int>> &qadj,
    const std::vector<NECNode*> &all_necs,
    const std::vector<NECNode*> &node2nec,
    std::vector<int> &M,
    std::vector<int> &F,
    CRTree *CR,
    std::vector<std::unordered_map<int,int>> &outMatches,
    int depth)
{
    dfs_total_calls++;

    // DFS timeout check (throttled: poll the clock every 8192 recursions to avoid steady_clock overhead)
    {
        static long long _dfs_check_cnt = 0;
        if (g_timeout_sec > 0 && ((++_dfs_check_cnt) & 1023) == 0) {
            if (std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
                g_timed_out = true;
            }
        }
        if (g_timed_out) return;
    }

    int u_prime = order[depth]; // current NEC id
    NECNode* nec = all_necs[u_prime];
    const std::vector<int> &qGroup = nec->vertex_set; // query nodes of the current NEC
    int k = static_cast<int>(qGroup.size());
    std::vector<int> pool;
    // Parent data-node array (for isJoinable)
    std::vector<int> parent_data_nodes;

    int parent_node_id = all_necs[u_prime]->parent ? all_necs[u_prime]->parent->id : -1;
        NECNode* parent_nec = all_necs[parent_node_id];
        const std::vector<int>& parent_query_nodes = parent_nec->vertex_set;
        int parenr_node_data = M[parent_query_nodes[0]]; // data node of the parent NEC first query node (assumes intra-parent clique)
        // Adaptive strategy: pick the best method by parent NEC size
        // Key insight: whether the parent NEC forms a clique does not affect child NECs; only its size matters

    if (parent_query_nodes.size() == 1) {
            // Fast path: parent NEC has a single query node
            // Use its CR entry directly
            int parent_q = parent_query_nodes[0];
            int parent_dn = M[parent_q];

            if (parent_dn == -1) {
                return;
            }
            int pos = -1;
            for (size_t t = 0; t < CR->parent[u_prime].size(); ++t) {
                if (CR->parent[u_prime][t] == parent_dn) {
                    pos = t;
                    break;
                }
            }

            if (pos == -1) {
                return;
            }

            // Copy the CR entry into the pool (avoid mutating through the reference)
            pool = CR->CR[u_prime][pos];
            parent_data_nodes.push_back(parent_dn);
    }
    else {
            // Multi-parent path: parent NEC has multiple query nodes
            parent_data_nodes.reserve(parent_query_nodes.size());
            for (int parent_q : parent_query_nodes) {
                int dn = M[parent_q];
                if (dn == -1) {
                    return;  // parent unmapped, cannot proceed
                }
                parent_data_nodes.push_back(dn);
            }
            std::vector<const std::vector<int>*> cr_entries;
            cr_entries.reserve(parent_query_nodes.size());
            for (int pdn : parent_data_nodes) {
                int pos = -1;
                for (size_t t = 0; t < CR->parent[u_prime].size(); ++t) {
                    if (CR->parent[u_prime][t] == pdn) {
                        pos = t;
                        break;
                    }
                }
                if (pos == -1) {
                    return;  // CR entry not found
                }
                cr_entries.push_back(&CR->CR[u_prime][pos]);
            }

            // Intersect (optimized std::set_intersection)
            // CR entries are assumed sorted
            std::vector<int> C1 = *cr_entries[0];  // start from the first

            for (size_t i = 1; i < cr_entries.size(); ++i) {
                const std::vector<int>* next_cr = cr_entries[i];
                std::vector<int> intersection;
                intersection.reserve(std::min(C1.size(), next_cr->size()));

                // Two-pointer intersection, O(n + m)
                size_t p1 = 0, p2 = 0;
                while (p1 < C1.size() && p2 < next_cr->size()) {
                    if (C1[p1] == (*next_cr)[p2]) {
                        intersection.push_back(C1[p1]);
                        p1++;
                        p2++;
                    } else if (C1[p1] < (*next_cr)[p2]) {
                        p1++;
                    } else {
                        p2++;
                    }
                }

                C1 = std::move(intersection);
                if (C1.size() < static_cast<size_t>(qGroup.size())) {
                    return;  // intersection too small to form an NEC
                }
            }

            pool = std::move(C1);
    }

    // Empty candidate pool: return (cannot match further)
    if (pool.empty()) {
        return;
    }

    // Stage 3 (moved up): compute neighbors to check; needed by the last-level fast isJoin check and requires no F

    // Precompute the data nodes whose connectivity must be checked for qGroup
    // Avoids repeated M[qnbr] lookups inside isJoinable
    // All qGroup nodes belong to one NEC with identical neighbor check lists, so compute once
    std::vector<int> neighbors_to_check;

    // Any qGroup node works (identical neighbor structure)
    int q = qGroup[0];
    for (int qnbr : qadj[q]) { // a qGroup neighbor may be an NEC at the same level, hence the dn != -1 filter
        int dn = M[qnbr];
        if (dn == -1) continue;
        bool is_parent = false;
        for (int pdn : parent_data_nodes) {
            if (dn == pdn) {
                is_parent = true;
                break;
            }
        }
        if (is_parent) continue;

        neighbors_to_check.push_back(dn);
    }
    
    
    
    if(depth == static_cast<int>(order.size()) - 1)
    {
        // [B0] Pendant-leaf O(1) fast counting (generalized from the hardcoded q6 version, no query-specific constants)
        // Trigger: k==1 (leaf NEC with a single query node) and that node has
        //           exactly one query neighbor (a pendant: leaf-parent, no other query edges).
        //           The original q6 hardcoded u_prime==4; here qadj[qGroup[0]].size()==1 is equivalent.
        // Idea: valid leaf candidates = unoccupied neighbors of parent data node m_parent.
        //       total neighbors = globalDegree[m_parent], minus two kinds of occupation:
        //         (a) query_nbr_occupied: query neighbors of the parent query node (except the leaf) are all matched;
        //             isomorphism guarantees their data images are neighbors of m_parent, so they are always occupied;
        //             count = qadj[q_parent].size() - 1 (minus the leaf edge itself);
        //             (matches the original q6 "-3": qadj[4]={0,1,3,5}, 4-1=3);
        //         (b) extra_occupied: matched query nodes with no query edge to the parent
        //             but with a data edge to m_parent; not covered by (a), deducted here.
        //             (matches the original q6 "if has_edge_in_data(m4,m2) addsize--":
        //              node 2 is not a query neighbor of 4, yet M[2] may be adjacent to m4 in data)
        if (k == 1 && qadj[qGroup[0]].size() == 1) {
            int q_leaf = qGroup[0];
            int q_parent = qadj[q_leaf][0];          // the leaf's only query neighbor = parent
            int m_parent = M[q_parent];
            // (a) query-neighbor occupation
            int query_nbr_occupied = (int)qadj[q_parent].size() - 1;
            // (b) extra occupation: scan matched query nodes for "non-query-neighbor with a data edge"
            int extra_occupied = 0;
            const std::vector<int>& nbrs_of_parent = qadj[q_parent];  // sorted (by buildQueryAdj)
            int qn = (int)qadj.size();
            for (int qd = 0; qd < qn; ++qd) {
                if (qd == q_parent || qd == q_leaf) continue;
                int md = M[qd];
                if (md == -1) continue;  // unmatched (M initialized to -1)
                // Binary-search whether qd is a query neighbor of q_parent:
                //   yes -> case (a), already deducted via query_nbr_occupied, skip;
                //   no  -> check the data edge; M[qd] adjacent to m_parent counts as extra occupation.
                // binary_search applies since nbrs_of_parent is sorted, O(log deg).
                if (std::binary_search(nbrs_of_parent.begin(), nbrs_of_parent.end(), qd)) continue;
                if (has_edge_in_data(md, m_parent)) extra_occupied++;
            }
            int addsize = globalDegree[m_parent] - query_nbr_occupied - extra_occupied;
            if (addsize < 0) addsize = 0;  // defensive (cannot be negative in theory)
            current_match_cnt += addsize;
            if (current_match_cnt >= matchtarget) return;
            return;
        }
        // q2 fast counter — decide whether the NEC forms a clique: count via GT-built adjacency if so, else count edges over the pool
        bool necIsClique = (k >= 2) &&
            std::binary_search(qadj[qGroup[0]].begin(), qadj[qGroup[0]].end(), qGroup[1]);
        if(!necIsClique)
        {
            long long pooljoinablecnt = 0;
            if(neighbors_to_check.size()==1) // generic way to save one isJoin call; q2 enters the size=1 branch
            {
                
                int parent_need_check_joinable = neighbors_to_check[0];
                for(int node:pool)
                {
                    if (g_timed_out) return;
                    if(F[node]!=g_f_epoch)
                    {
                        if(has_edge_in_data(node, parent_need_check_joinable))
                        pooljoinablecnt++;//necsizelast = 1 AN1=N
                    }
                }

            }
            else
            {
                for(int node:pool)
                {
                    if(F[node]!=g_f_epoch && isJoinable_usinghas_edge_in_data(node, neighbors_to_check))
                    {
                        pooljoinablecnt++;
                    }
                }

            }
            current_match_cnt += pernum_A_M_N(pooljoinablecnt, qGroup.size());
            // current_match_cnt += pooljoinablecnt;

        }
        else {
            std::vector<int> filtered_pool;
            filtered_pool.reserve(pool.size());
            for (int node : pool) {
                if (g_timed_out) return;
                if (F[node] == g_f_epoch) continue;
                if (isJoinable_usinghas_edge_in_data(node, neighbors_to_check)) {
                    filtered_pool.push_back(node);
                }
            }
            int psz = (int)filtered_pool.size();
            vector<vector<int>> nb(psz);  // nb[i] sorted: {j | j>i, has_edge(filtered_pool[i], filtered_pool[j])}
            for (int i = 0; i < psz; ++i) {
                int u = filtered_pool[i];
                for (int j = i + 1; j < psz; ++j) {
                    if (has_edge_in_data(u, filtered_pool[j]))
                        nb[i].push_back(j);
                }
            }
            long long kc = 0;
            if (k == 2) {
                for (int i = 0; i < psz; ++i) kc += (int)nb[i].size();  // count each edge once
            } else if (k == 3) {
                for (int i = 0; i < psz; ++i) {
                    const auto& nbi = nb[i];
                    for (size_t j_idx = 0; j_idx < nbi.size(); ++j_idx) {
                        int j = nbi[j_idx];
                        const auto& nbj = nb[j];
                        size_t p1 = 0, p2 = 0;
                        while (p1 < nbi.size() && p2 < nbj.size()) {
                            int x = nbi[p1], y = nbj[p2];
                            if (x == y) { kc++; p1++; p2++; }
                            else if (x < y) p1++; else p2++;
                        }
                    }
                }
            } else {
                // k >= 4: fallback branch, count combination by combination with ComboIterator
                // Reuses filtered_pool (already isJoinable-filtered); ComboIterator with needClique=true
                // checks internal edges, but for a clique NEC every combo should be a clique in theory
                ComboIterator comboIter(filtered_pool, k, true);  // needClique=true
                if (!comboIter.finished) {
                    do {
                        // comboIter already guarantees a clique (needClique=true)
                        // each combo corresponds to k! permutations
                        kc++;
                    } while (comboIter.next());
                }
            }
            // k! = pernum_A_M_N(k, k) = k × (k-1) × ... × 1
            current_match_cnt += kc * pernum_A_M_N(k, k);
            if (current_match_cnt >= matchtarget) return;
            return;
        }

        return;
    }

    if(1==k)
    {
        // Single-node NEC: skip combination/permutation generation, iterate the pool directly
        // C(n,1)=n, no combination iterator needed
        // a single element has one permutation, no next_permutation needed
        // const auto& neighbors_to_check_ref = neighbors_to_check;
        for (int node : pool)
        {
            // Timeout check (pool can be huge; a single pass can stall, so check inside the loop)
            if (g_timed_out) return;
            // F filter (skip already-used nodes)
            if(F[node]==g_f_epoch)
            {
                continue;
            }

            // Joinability check
            // Check if joinable with existing neighbors
            if (!isJoinable_usinghas_edge_in_data(node, neighbors_to_check)) {
                continue;
            }

            // Update state
            // UpdateState(M, F, qGroup, single_node_combo);
            F[node] = g_f_epoch;
            M[qGroup[0]] = node;

            // Recurse
            subgraphSearch_TurboIso_rec(order, qadj, all_necs, node2nec,
                                      M, F, CR, outMatches, depth + 1);
            if (current_match_cnt >= matchtarget) {
                return;
            }

            // Restore state
            // RestoreState(M, F, qGroup, single_node_combo);
            F[node] = 0;
            M[qGroup[0]] = -1;
        }
        return;  // k=1 handled, done
    }

    // F-filter the pool (drop used nodes) so combination generation need not re-check F
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < pool.size(); ++read_idx) {
        if (g_timed_out) return;
        if (F[pool[read_idx]]!=g_f_epoch) {
            pool[write_idx++] = pool[read_idx];
        }
    }
    pool.resize(write_idx);
    if (pool.empty()) {
        return;
    }

   
        int memberA = qGroup[0];
        int memberB = qGroup[1];
        bool necIsClique = std::binary_search(qadj[memberA].begin(), qadj[memberA].end(), memberB);

    // Combination generation
    // Use efficient combination iterator (replaces batch generation)
    ComboIterator comboIter(pool, k, necIsClique);
    if (!comboIter.finished) {
        do {
            const auto& combo = comboIter.getCombo();

        // Joinability check
        // Check if joinable with existing neighbors
        bool ok = true;
        for (size_t i = 0; i < qGroup.size(); ++i) {
            if (!isJoinable_usinghas_edge_in_data(combo[i], neighbors_to_check)) {
                ok = false;
                break;
            }
        }

        if (!ok) continue;

        
         // Recursion and permutation generation
        // Use next_permutation to generate permutations iteratively
        std::vector<int> perm = combo;
        long long match_cnt_before = current_match_cnt;
        do {
            //     current_match_cnt += delta;
            // Update state
            UpdateState(M, F, qGroup, perm);

            // Recurse
            subgraphSearch_TurboIso_rec(order, qadj, all_necs, node2nec,
                                      M, F, CR, outMatches, depth + 1);

            // target reached: return directly, no state restore needed
            if (current_match_cnt >= matchtarget) {
                return;
            }

            // Restore state
            RestoreState(M, F, qGroup, perm);

            long long delta = current_match_cnt - match_cnt_before;
            current_match_cnt += delta;
            break;

        } while (std::next_permutation(perm.begin(), perm.end()));
        } while (comboIter.next());
    } // if (!comboIter.finished)
}

// TurboISO start-vertex selection (original kept)
int chooseStartQVertex_TurboIso_runtime(
    const QueryGraph &Q)
{
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    cout << "\n=== [TurboIso] Stage: ChooseStartQVertex ===" << endl;
    // -------------------------------------------------------
    // Step 1. Scan all superNodes to count data label frequency
    // -------------------------------------------------------
    // labels disabled (uniform "user"): original labelFreq scan removed
    // unordered_map<string, int> labelFreq;
    // size_t total_nodes_scanned = 0;
    //         total_nodes_scanned += sn.size;
    size_t total_nodes_scanned = 0;

    auto t1 = high_resolution_clock::now();
    double scan_time_ms = duration_cast<microseconds>(t1 - t0).count() / 1000.0;

    cout << "[INFO] Scanned " << total_nodes_scanned
         << " data nodes for label frequency in "
         << fixed << setprecision(3) << scan_time_ms << " ms" << endl;

    // -------------------------------------------------------
    // Step 2. Compute rank[u] for each query vertex
    // -------------------------------------------------------
    int best_vertex = -1;
    double best_rank = numeric_limits<double>::infinity();

    for (int i = 0; i < (int)Q.node_ids.size(); ++i)
    {
        // string label = Q.node_labels.empty() ? "_default" : Q.node_labels[i];

        // count degree of node i in query
        int degree = 0;
        for (auto &e : Q.edges)
            if (e.first == i || e.second == i)
                degree++;

        // compute label frequency
        double freq = 1.0;
        // else
            // unseen label — make it large to avoid selection
            freq = 1e9;

        double rank = freq / (degree + 1.0);

        if (rank < best_rank)
        {
            best_rank = rank;
            best_vertex = i;
        }
    }

    auto t2 = high_resolution_clock::now();
    double rank_time_ms = duration_cast<microseconds>(t2 - t1).count() / 1000.0;

    // -------------------------------------------------------
    // Step 3. Output result
    // -------------------------------------------------------
    cout << "[INFO] Rank computation time: "
         << fixed << setprecision(3) << rank_time_ms << " ms" << endl;
    cout << "[INFO] Selected start query vertex: "
         << best_vertex << " (rank = " << best_rank << ")\n";
    cout << "=============================================\n";

    return best_vertex;
}

Matches runMatch(const QueryGraph &Q) {
    cout << "--------------------runMatch" << endl;
    g_timed_out = false;
    // SIGALRM is registered in main; only reset the timer origin here (timeout counts from match start)
    // To count from main instead, comment out the next line
    // g_subac_t0 = std::chrono::steady_clock::now();
            
    Matches results;

    // 1) build qadj
    std::vector<std::vector<int>> qadj;
    buildQueryAdj(Q, qadj);
    int qn = (int)Q.node_ids.size();
    if (!qn) return results;
    // 4) build NEC tree

    std::vector<NECNode*> all_necs;
    std::vector<NECNode*> node2nec; // size qn, node2nec[qi] -> NECNode*
    int start_q = chooseStartQVertex_TurboIso_runtime(Q);
    
    // Newer variant: does not touch supernodes, estimates via the paper rankU formula
    NECNode* nec_root = buildNECTree(Q, start_q, qadj, all_necs, node2nec);

    // Print the query tree
    std::cout << "=== NEC Tree Structure ===" << std::endl;
    std::vector<std::vector<int>> candidates;
    int qStartId=nec_root->vertex_set[0];
    int qStartDegree=qadj[qStartId].size();
    bool excludePathMidNode=false;
    if(qStartDegree>2)
    {
        excludePathMidNode=true;
    }
    else if(qStartDegree==2)
    {
        int v1,v2;
        v1=qadj[qStartId][0];
        v2=qadj[qStartId][1];
        if(binary_search(qadj[v1].begin(), qadj[v1].end(), v2))
        {
            excludePathMidNode=true; // forms a triangle; path middle nodes must not appear in it
        }
    }
    int root_query_vertex = qStartId;
   

    // ========== Use the fixed NEC match order (g_fixed_nec_order) directly, no computation ==========
    // NEC structure and order depend only on query topology; one order per query, see the global table comment.
    auto fit = g_fixed_nec_order.find(g_query_id);
    if (fit == g_fixed_nec_order.end()) {
        std::cerr << "[ERROR] q" << g_query_id << " has no fixed NEC order" << std::endl;
        std::exit(1);
    }
    std::vector<int> order = fit->second;
    std::cout << "[Fixed NEC Order] q" << g_query_id << " order:";
    for (size_t _i = 0; _i < order.size(); ++_i) std::cout << " " << order[_i];
    std::cout << std::endl;

    // auto exploreCR_start = std::chrono::steady_clock::now();

    // visited defined outside the loop (epoch scheme: int array with current_epoch, avoids full assign)
    std::vector<int> visited;
    visited.assign(numOriNodes, 0);  // initialized once outside the loop, never reset inside

    // Preallocate M and F (once outside the loop, reused across start vertices)
    std::vector<int> M;
    M.reserve(qn);
    std::vector<int> F;
    F.assign(numOriNodes, 0);  // epoch scheme: init once outside, g_f_epoch++ inside

    for (size_t super_idx = 0; super_idx < super_order.size(); ++super_idx) {
        int super_id = super_order[super_idx];  // iterate in sorted order
        if (g_timeout_sec > 0 &&
            std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
            g_timed_out = true;
            break;
        }
        // ===== single guard: single supernodes (id >= firstSingleId) have no synopsis, treated as original nodes =====
        //   md   -> use the sole member's globalDegree (the member node, not super_id)
        //   type -> always single (never path; the else branch returns all members)
        //   candidates -> superNodeMembers kept intact; the single region returns a one-element set as usual
        bool is_single = (super_id >= firstSingleId);

        // --- coarse md pruning ---
        if (is_single) {
            int member = superNodeMembers[super_id][0];   // the sole member (not super_id itself)
            if (globalDegree[member] < qStartDegree) continue;
        } else {
            if (superNodes[super_id].md < qStartDegree) continue;
        }

        // // label cut  ignore label

        vector<int>validDataNodes;
        // path middle node cut if flg
        // singles never enter the path branch (!is_single short-circuit avoids reading an empty synopsis)
        if(!is_single && excludePathMidNode && superNodes[super_id].type==4)  // path
        {
            // Upstream issue (not downstream): on CTR (adj1-verified) the synopsis begin = tail-neighbor (degree 2),
            // end = tail, so begin is not the chain head (the members front is). Will be correct once contraction fixes it.
            // Downstream follows the "self-contained synopsis" design: read synopsis, never re-derive from members.
            validDataNodes.push_back(superNodes[super_id].beginnode);
            validDataNodes.push_back(superNodes[super_id].endnode);
            // keep only the path endpoints as candidates (shape pruning)
        }
        else
        {
            validDataNodes = superNodeMembers[super_id];
        }
        for (int start_data_vertex : validDataNodes) { // supernode order
            // ===== Timeout check (inner) + heartbeat =====
            if (g_timeout_sec > 0 &&
                std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
                g_timed_out = true;
                break;
            }
            CRTree CR;
            CR.init((int)all_necs.size());
            std::vector<int> VM({start_data_vertex});

            // Timeout check before assign (avoid being stuck inside assign past the ExploreCR entry check)
            if (g_timeout_sec > 0 && g_timed_out == false) {
                if (std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
                    g_timed_out = true;
                }
            }
            if (g_timed_out) break;

            // epoch scheme: incrementing the epoch invalidates all old visited values (replaces full assign)
            g_current_epoch++;

            auto exploreCR_start11 = std::chrono::steady_clock::now();
            bool explore_success = ExploreCR(0, &VM, &CR, -1, all_necs, qadj, Q, visited); // start-vertex degree check lives inside ExploreCR
            auto exploreCR_end11 = std::chrono::steady_clock::now();
            std::chrono::duration<double> exploreCR_duration = exploreCR_end11 - exploreCR_start11;
            explorecr_time_ms += exploreCR_duration.count() * 1000.0;
            exploreCR_call_count++;

            if (explore_success) {
                // Reuse preallocated M and F
                M.assign(qn, -1);  // reset to -1
                g_f_epoch++; /* replaces a full F.assign reset */

                // Initialize the mapping: root query node -> start data vertex
                int root_query_vertex = nec_root->vertex_set[0];

                M[root_query_vertex] = start_data_vertex;
                F[start_data_vertex] = g_f_epoch;
                
                // Run the subgraph search (pass the CR tree directly, no candidates)

                // ========== Total DFS timing (accumulated over all depths) ==========
                auto dfs_total_start = std::chrono::steady_clock::now();

                subgraphSearch_TurboIso_rec(order, qadj, all_necs, node2nec,
                            M, F, &CR, results, 1);

                auto dfs_total_end = std::chrono::steady_clock::now();
                dfs_total_time_all_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(dfs_total_end - dfs_total_start).count();

                if(current_match_cnt >= matchtarget)
                {
                    for (NECNode* n : all_necs) delete n;
                    return results;
                }
                M[root_query_vertex] = -1;
                F[start_data_vertex] = 0;
            }
        }//end for node v
    } // end for supernode

    for (NECNode* n : all_necs) delete n;

    if (g_timed_out) {
        std::cout << "[TIMEOUT] reached g_timeout_sec=" << g_timeout_sec
                  << "s, current_match_cnt=" << current_match_cnt << std::endl;
        // ===== Phase-time diagnostics (printed on timeout too, to locate the stall) =====
        std::cout << "\n========== STAGE BREAKDOWN (timeout) ==========" << std::endl;
        std::cout << "ExploreCR total time: " << (explorecr_time_ms / 1000.0) << " s" << std::endl;
        std::cout << "DFS (subgraphSearch) total time: " << (dfs_total_time_all_ns / 1e9) << " s" << std::endl;
        std::cout << "ExploreCR outer calls (per start_data_vertex): " << exploreCR_call_count << std::endl;
        std::cout << "ExploreCR recursive calls: " << explorecr_calls << std::endl;
        std::cout << "has_edge_in_data total calls: " << has_edge_in_data_total_calls << std::endl;
    }
    return results;
}
// ------------------------------ Loading and preprocessing -------------------
// Query graph builders and result printing
// Medium-size clique queries (complete graphs of 5-7 nodes)

// ========== Combined six-in-one query graph dispatcher ==========
// Selects the query graph by qid instead of maintaining six cpp files.
// Graph structures come from the create_test_query_* functions of the original per-q files, verified one by one.
// qid | nodes | edges | structure
// ----|--------|------|----------
//  0  |   4    |  6   | 4-clique (complete graph K4)
//  2  |   4    |  5   | diamond (K4 minus one edge)
//  3  |   5    |  6   | 5-cycle + one chord (0-2)
//  5  |   5    |  8   | 5-node special graph (degrees 0=2,1=4,2=4,3=3,4=3)
//  6  |   6    |  9   | 6-cycle + three chords (0-2,0-3,0-4)
//  7  |   6    |  9   | 6 nodes 9 edges (with pendant 5-4)
QueryGraph create_query(int qid) {
    QueryGraph Q;

    auto add_nodes = [&](int n) {
        for (int i = 0; i < n; ++i) {
            Q.node_ids.push_back(i);
        }
    };

    switch (qid) {
        case 1: {
            // 4-clique: complete graph K4
            add_nodes(4);
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j)
                    Q.edges.push_back({i, j});
            // pernumk as in the original q1 ((clique_size-1)!, = 3! = 6 for a 4-clique)
            pernumk = 1;
            for (int i = 2; i <= 3; ++i) pernumk *= i;
            break;
        }
        case 2: {
            // Diamond (q2): K4 minus edge 1-3
            //     0
            //    / \
            //   1---2
            //    \ /
            //     3
            add_nodes(4);
            Q.edges.push_back({0, 1});
            Q.edges.push_back({1, 2});
            Q.edges.push_back({2, 3});
            Q.edges.push_back({0, 3});
            Q.edges.push_back({0, 2});
            break;
        }
        case 3: {
            // 5-cycle + chord 0-2 (q3): cycle 0-1-2-3-4-0
            add_nodes(5);
            Q.edges.push_back({0, 1});
            Q.edges.push_back({1, 2});
            Q.edges.push_back({2, 3});
            Q.edges.push_back({3, 4});
            Q.edges.push_back({4, 0});
            Q.edges.push_back({0, 2});
            break;
        }
        case 4: {
            // 5-node special graph (q4): degrees 0=2,1=4,2=4,3=3,4=3
            add_nodes(5);
            Q.edges.push_back({0, 1});
            Q.edges.push_back({0, 2});
            Q.edges.push_back({1, 2});
            Q.edges.push_back({1, 3});
            Q.edges.push_back({1, 4});
            Q.edges.push_back({2, 3});
            Q.edges.push_back({2, 4});
            Q.edges.push_back({3, 4});
            break;
        }
        case 5: {
            // 6-cycle + chords 0-2,0-3,0-4 (q5): cycle 0-1-2-3-4-5-0
            add_nodes(6);
            Q.edges.push_back({0, 1});
            Q.edges.push_back({1, 2});
            Q.edges.push_back({2, 3});
            Q.edges.push_back({3, 4});
            Q.edges.push_back({4, 5});
            Q.edges.push_back({5, 0});
            Q.edges.push_back({0, 2});
            Q.edges.push_back({0, 3});
            Q.edges.push_back({0, 4});
            break;
        }
        case 6: {
            // q6 graph: 6 nodes 9 edges, with pendant 5-4
            //       0
            //      /|\
            //     1-+-4---5
            //     |/ \|
            //     2---3
            add_nodes(6);
            Q.edges.push_back({0, 1});
            Q.edges.push_back({0, 2});
            Q.edges.push_back({0, 3});
            Q.edges.push_back({0, 4});
            Q.edges.push_back({1, 2});
            Q.edges.push_back({1, 4});
            Q.edges.push_back({2, 3});
            Q.edges.push_back({3, 4});
            Q.edges.push_back({4, 5});
            break;
        }
        default:
            std::cerr << "[ERROR] unknown qid=" << qid
                      << " (supported: 1 2 3 4 5 6)" << std::endl;
            std::exit(1);
    }

    return Q;
}

void print_matches(const vector<unordered_map<int, int>>& all_matches) {
    cout << "Found " << current_match_cnt << " matches:" << endl;
}

int main(int argc, char** argv) {
    // 3rd arg = query id qid (1-6, contiguous numbering)
    // Optional 4th arg: trial_count (default 1, passed as TRIALS by scripts)
    if (argc < 4) {
        cout << "Usage: " << argv[0] << " <match_target> <dataset_name> <query_id> [trial_count]" << endl;
        cout << "       supported query_id: 1 2 3 4 5 6" << endl;
        return 1;
    }
    string dataset_name = argv[2];
    int query_id = std::stoi(argv[3]);
    g_query_id = query_id;  // used by query-specific logic in runMatch (e.g. the q2 manual order)

    // Optional 4th arg = trial_count, default 1
    int trial_count = 1;
    if (argc >= 5) {
        trial_count = std::max(1, std::atoi(argv[4]));
    }

    // Register the SIGALRM soft timeout at the start of main (covers graph loading + matching)
    // Wherever we are stuck, alarm_handler prints accumulated CR/DFS times and exits on fire
    g_subac_t0 = std::chrono::steady_clock::now();
    if (g_timeout_sec > 0) {
        signal(SIGALRM, alarm_handler);
        alarm((unsigned)g_timeout_sec);
    }

    // Build the path prefix from the dataset name
    string output_prefix = "../data/output_" + dataset_name + "/";
    string gtbin_path = "../data/gtbin/" + dataset_name + ".bin";

    cout << "Dataset: " << dataset_name << endl;

    // Common data loading ---------------------------------
    readSummaryStats((output_prefix + "summary.txt").c_str());
    cout<<"From summary.txt: numRemainNodes: "<<numRemainNodes<<", supernodenum: "<<supernodenum<<", numOriNodes: "<<numOriNodes<<endl;
    cout<<"numRemainNodes: "<<numRemainNodes<<endl;
    cout<<"numOriNodes: "<<numOriNodes<<endl;
    adjExternal.resize(numOriNodes);
    cout<<"adjExternal size: "<<adjExternal.size()<<endl;
    cout<<"supernodenum: "<<supernodenum<<endl;

    // id mapping: maps new (contracted) ids back to original ones
    loadIdMapping((output_prefix + "id_mapping.txt").c_str());

    superNodeMembers.resize(supernodenum);
    superAdj.resize(supernodenum);
    loadMappingTxt((output_prefix + "mapping.txt").c_str());   // fills nodeToSuper only
    loadMembersTxt((output_prefix + "members.txt").c_str());   // fills superNodeMembers (order-preserving, incl. singles)
    globalDegree.clear();
    globalDegree.resize(numOriNodes);
    loadExternalAdj((output_prefix + "external_adj.txt").c_str());
    loadBitCodesBinary(gtbin_path.c_str());

    // Load the synopsis into the supernode struct vector
    superNodes = loadSuperGraph((output_prefix + "synopsis.txt").c_str());

    // Supernode sorter: type rank (single<path<star<clique) + md
    // superNodes itself is not reordered (that would break nodeToSuper/superNodeMembers indices); only an index order is built.
    super_order.resize(superNodes.size());
    for (size_t i = 0; i < superNodes.size(); ++i) super_order[i] = (int)i;

    // ========== Supernode sorter (to switch variants, change type_rank returns and the md comparison) ==========
    // Active variant: (1) clique-first + md ascending
    // The single region (id >= firstSingleId) has no synopsis; never read sa.type/sa.md there;
    //       singles always sort last (rank=3), stably ordered by id among themselves.
    auto type_rank = [](uint8_t t) -> int {
        // clique first (clique<star<path<single); t: 1=clique 2=star 3=diamond 4=path 5=single
        if (t == 1) return 0;  // clique
        if (t == 2) return 1;  // star
        if (t == 3) return 2;  // diamond (same tier as path; just not single)
        if (t == 4) return 2;  // path
        return 3;  // single (5) or anything else -> last
    };
    std::sort(super_order.begin(), super_order.end(), [&](int a, int b) {
        bool a_single = (a >= firstSingleId);
        bool b_single = (b >= firstSingleId);
        if (a_single && b_single) return a < b;   // both single: by id ascending
        if (a_single) return false;                // a single, b not -> a goes last
        if (b_single) return true;                 // b single, a not -> a goes first
        const SuperNode& sa = superNodes[a];       // neither single: synopsis safe to read
        const SuperNode& sb = superNodes[b];
        int ra = type_rank(sa.type), rb = type_rank(sb.type);
        if (ra != rb) return ra < rb;
        return sa.md < sb.md;  // md ascending
    });

    // ----- Other variants (kept for reference; swap type_rank + the md comparison to switch) -----
    // (2) clique-first + md descending:
    //   type_rank: clique=0, star=1, path=2, single=3
    //   md comparison: return sa.md > sb.md;
    // (3) clique-last + md ascending:
    //   type_rank: single=0, path=1, star=2, clique=3
    //   md comparison: return sa.md < sb.md;
    // (4) clique-last + md descending (best overall for q6):
    //   type_rank: single=0, path=1, star=2, clique=3
    //   md comparison: return sa.md > sb.md;
    // Common data loading -------------------------------------------------

    // This algorithm needs the internal adjacency
    // Internal adjacency: traversing all neighbors and degree pruning rely on it
	adjInternal.resize(numOriNodes);
	loadInternalAdj((output_prefix + "internal_adj.txt").c_str());

	// O(1) intra-path edge structure (if missing, g_path_pos_ok=false and the path branch falls back to O(N))
	loadPathPos(output_prefix + "path_pos.txt");

	// Sort adjInternal/adjExternal for binary_search in clique-edge verification
	for (int i = 0; i < numOriNodes; ++i) {
	    sort(adjInternal[i].begin(), adjInternal[i].end());
	    sort(adjExternal[i].begin(), adjExternal[i].end());
	}

	// Build the merged adjacency adjall: internal + external
	mergeAdjacentLists();

    // Labels: fill dataNodeLabel with "user" for all data nodes (label-free setting),
    dataNodeLabel.resize(numOriNodes);
    for(int i = 0; i < numOriNodes; i++) {
        dataNodeLabel[i] = "user";
    }

    cout << "=== SubA<sub>A</sub> Algorithm Demo ===" << endl;

    matchtarget= std::stoi(argv[1]);
    QueryGraph Q = create_query(query_id);

    size_t gt_memory_footprint = gtCode.size() * sizeof(BitCode);
    std::cout << "GT data size: " << gt_memory_footprint / (1024 * 1024) << " MB\n";

    // k (iterations) is the 4th CLI arg trial_count (default 1) so scripts can control repetitions
    const int k = trial_count;
    std::vector<double> durations;
    exploreCR_call_count = 0;
    for (int iter = 0; iter < k; iter++) {  
        no_edge_and_gt_get = 0;
        no_edge_but_gt_noget = 0;
        have_edge_and_gt_noget = 0;
        internal_edge_true_cnt = 0;
        internal_edge_false_cnt = 0;
        // no_superedge_and_gt_get = 0;
        // no_superedge_but_gt_noget = 0;
        // have_superedge_and_gt_noget = 0;
        current_match_cnt = 0;

        has_edge_in_data_total_calls = 0;
        dfs_total_calls = 0;
        explorecr_calls = 0;
        // Reset timers too (previously only counters were reset; across trials they accumulated and broke the /k average)
        explorecr_time_ms = 0.0;
            exploreCR_call_count = 0;
        dfs_total_time_all_ns = 0;

        auto start = std::chrono::steady_clock::now();
        // TURBOISO
        vector<unordered_map<int, int>> results = runMatch(Q);
        auto end = std::chrono::steady_clock::now();

        std::chrono::duration<double> duration = end - start;
        durations.push_back(duration.count());
        print_matches(results);
    }
    double avg_duration = 0.0;
    for (double d : durations) avg_duration += d;
    avg_duration /= k;
    std::cout << "has gt Average time over " << k << " iterations: " << avg_duration << " seconds\n";
	
	// ========== Performance statistics ==========
	std::cout << "\n========== DFS ==========" << std::endl;
	std::cout << "Total DFS time (all depths): " << (dfs_total_time_all_ns / 1e9 / k) << " s" << std::endl;
	std::cout << "Total DFS calls: " << dfs_total_calls << std::endl;

	std::cout << "internal_edge_true:  " << internal_edge_true_cnt << "\n";
	std::cout << "internal_edge_false: " << internal_edge_false_cnt << "\n";
#ifndef NO_GT_STATS
	long long inside_edge = has_edge_in_data_total_calls - no_edge_and_gt_get - no_edge_but_gt_noget - have_edge_and_gt_noget;
	std::cout << "\n--- GT Statistics ---" << std::endl;
	std::cout << "has_edge_in_data calls: " << has_edge_in_data_total_calls << std::endl;
	std::cout << "  internal:        " << inside_edge << std::endl;
	std::cout << "  int_edge:        " << internal_edge_true_cnt << std::endl;
	std::cout << "  int_nonedge:     " << internal_edge_false_cnt << std::endl;
	std::cout << "  GT hit (skip bs):" << no_edge_and_gt_get << std::endl;
	std::cout << "  GT miss (non-edge):" << no_edge_but_gt_noget << std::endl;
	std::cout << "  edge (GT miss):  " << have_edge_and_gt_noget << std::endl;
	if (has_edge_in_data_total_calls > 0)
		std::cout << "GT hit rate:       " << (100.0*no_edge_and_gt_get/has_edge_in_data_total_calls) << "%" << std::endl;
#endif

	std::cout << "\n========== ExploreCR ==========" << std::endl;
	std::cout << "CR build time: " << (explorecr_time_ms / k) << " ms" << std::endl;
	std::cout << "CR recursive calls: " << explorecr_calls << std::endl;

	return 0;
} 
