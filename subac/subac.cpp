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

// ========== GT 编码开关 ==========
// DISABLE_GT: 关闭 GT, has_edge_in_data 直接走二分查找
// NO_GT_STATS: 不记录 GT 计数器 (排除计数器递增开销)
// #define DISABLE_GT
// #define NO_GT_STATS

// ========== 全局变量定义（算法专属; 图全局已收进 ../common/graph.cpp） ==========

int exploreCR_call_count = 0;

// 超图结构
vector<int> super_order;  // 排序后的超节点索引 (主: type反序, 次: md降序)

// 标签和度数
vector<string> dataNodeLabel;

// GT命中指标
long long no_edge_and_gt_get = 0;
long long no_edge_but_gt_noget = 0;
long long have_edge_and_gt_noget = 0;

// 内部边查询细分: 查到边(判定有边) + 查到非边(判定无边) = 内部边查询总数
long long internal_edge_true_cnt = 0;
long long internal_edge_false_cnt = 0;

// 匹配计数
long long matchtarget = -1;
long long current_match_cnt = 0;

// ===== 超时控制（墙钟，秒）。0 表示不限 =====
long long g_timeout_sec = 300;
std::chrono::steady_clock::time_point g_subac_t0;
bool g_timed_out = false;

// visited 版本号: 避免 visited.assign 全量重置 (ctr 卡死真凶)
// visited[v] == current_epoch 表示已访问, 每次 start_data_vertex 循环 current_epoch++ 即失效旧值
int g_current_epoch = 0;
int g_f_epoch = 0;  // F 版本号 (避免 F.assign 全量重置)

int pernumk = -1;  // q1 clique 排列数 (4-clique 时 = 3! = 6)

// 当前查询图编号 (main 设定, runMatch 读取, 用于 q2 手动 order 覆盖等查询特定逻辑)
int g_query_id = -1;

// ========== 0903: 各查询图固定 NEC 匹配顺序 ==========
// 来自 旧版 generateRIOrder_NEC 在 bk 上的实测 dump (q2 为当时手动覆盖值):
//   q1: 0 1          NEC0{0} → NEC1{1,2,3}
//   q2: 0 2 1        NEC0{0} → NEC2{2} → NEC1{1,3}
//   q3: 0 2 1 3 4    NEC0{0} → NEC2{2} → NEC1{1} → NEC3{4} → NEC4{3}
//   q4: 0 3 2 1      NEC0{1}(根=查询顶点1) → NEC3{3,4} → NEC2{2} → NEC1{0}
//   q5: 0 2 3 4 1 5  全单点 NEC
//   q6: 0 1 3 2 4    NEC0{0} → NEC1{1,3} → NEC3{4} → NEC2{2} → NEC4{5}
// NEC 结构与 order 只依赖查询图拓扑, 与数据图无关, 故直接写死;
// generateRIOrder_NEC 保留不调用, 需要重新计算时可恢复。
static const std::map<int, std::vector<int>> g_fixed_nec_order = {
    {1, {0, 1}},
    {2, {0, 2, 1}},
    {3, {0, 2, 1, 3, 4}},
    {4, {0, 3, 2, 1}},
    {5, {0, 2, 3, 4, 1, 5}},
    {6, {0, 1, 3, 2, 4}},
};

// DFS性能分析全局变量
long long dfs_total_calls = 0;
long long dfs_total_time_all_ns = 0;
long long has_edge_in_data_total_calls = 0;

// ExploreCR性能统计
double explorecr_time_ms = 0.0;
long long explorecr_calls = 0;

// ===== SIGALRM 软超时: 到点强制打印分阶段时间后退出 (无论卡在哪) =====
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

// 排列数 A(n, k) = n! / (n-k)! = n × (n-1) × ... × (n-k+1)
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

// =============== 基于 main.cpp 的 TurboISO 决定顺序函数 ===============

// 比较 Elem 的函数（用于排序）
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

// 递归决定匹配顺序函数（基于 main.cpp 的实现）
// 参数说明：
// - nec: 当前 NEC 节点
// - CR: 候选区域树
// - qadj: 查询图邻接表
// - order: 输出的顺序数组
// - product: 累积的乘积因子
// - all_necs: 所有 NEC 节点（用于边界检查）
double determineMatchOrder(
        NECNode* nec,
        CRTree* CR,
        const std::vector<std::vector<int>>& qadj,
        Elem* order,
        int product,
        const std::vector<NECNode*>& all_necs)
{
    // 计算边数 ENum（不包括 NEC 内部边）
    int ENum = 0;
    int nSize = nec->vertex_set.size();
    for (int ii = 0; ii < nSize; ii++)
    {
        int vv = nec->vertex_set[ii];
        int graS = qadj[vv].size();
        for (int i = 0; i < graS; i++)
            ENum += 1; // main.cpp 中统计的是边的数量
    }

    // 如果 NEC 大小 > 1，减去内部边数
    if (nSize > 1)
    {
        // 检查 NEC 内部是否有边
        int vv0 = nec->vertex_set[0];
        int vv1 = nec->vertex_set[1];
        for (int nb : qadj[vv0])
        {
            if (nb == vv1)
            {
                // NEC 内部有边，减去 nSize*(nSize-1)/2
                ENum -= nSize * (nSize - 1) / 2;
                break;
            }
        }
    }

    // 如果有子节点，递归处理
    if (!nec->children.empty())
    {
        int Product;
        if (nec->parent == nullptr) // 根节点
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

        // 递归计算所有子节点的得分，选择最小值
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
    else // 叶子节点
    {
        int eNum = nSize * nec->parent->vertex_set.size();
        int Product = product * (ENum - eNum + 1);
        int nec_id = nec->id;

        if (nSize == 1)
        {
            // NEC 大小为 1：候选节点总数 / Product
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
            // NEC 大度 > 1：组合数总和 / Product
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

// 包装函数：调用 main.cpp 版本的决定顺序函数

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

    // 分配 order 数组
    Elem* order_arr = new Elem[all_necs.size()];

    // 调用递归函数（从根节点开始，product=1）
    determineMatchOrder(root, CR, qadj, order_arr, 1, all_necs);

    // 排序（按得分从小到大）
    qsort(order_arr, all_necs.size(), sizeof(Elem), cmpElem);

    // 输出到 order 向量
    for (size_t i = 0; i < all_necs.size(); i++)
    {
        order.push_back(order_arr[i].v);
        int nec_id = order_arr[i].v;
        NECNode* nec = all_necs[nec_id];
        int nSize = nec->vertex_set.size();
        double score = order_arr[i].value;

        // 计算Num和Product用于调试
        if (nSize == 1) {
            int Num = 0;
            if (CR && CR->CR && nec_id >= 0 && nec_id < (int)all_necs.size()) {
                auto &entries = CR->CR[nec_id];
                for (auto &pool : entries)
                    Num += pool.size();
            }
            // 反推Product: score = Num / Product → Product = Num / score
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

    // // 改为先查内部边
    if(nodeToSuper[u]==nodeToSuper[v])
    {
        // 内部边独立计时
        // TIMER_START(internal_edge);
        int SUPERU = nodeToSuper[u];
        // 0809 single 守卫: single 超节点仅 1 成员, 同超节点意味着 u==v(自环)。
        //   无摘要可读, 子图同构无自环 → 返回 false。
        if (SUPERU >= firstSingleId) {
            internal_edge_false_cnt++;   // single 自环判非边
            return false;
        }
        // 分类讨论，分形状 (type 改 uint8_t, 1=clique 2=star 3=diamond 4=path 5=single)
        auto supertype = superNodes[SUPERU].type;

        // 【Bug 2 修复 - 精准版】仅 path 类型需 adjInternal 验证。
        // 图收缩构建 path 时不能保证连续节点在原图中真相邻（ctr 实测 58% 假边率）。
        // clique 和 star 的形状判断经验证可靠。
        if(supertype==1)  // clique
        {
            internal_edge_true_cnt++;    // clique 内部对均有边
            return true;  // clique: 所有内部对均有边
        }
        else if(supertype==2)  // star
        {
            int center = superNodes[SUPERU].beginnode;
            if(u==center || v==center) {
                internal_edge_true_cnt++;    // star 中心-叶子
                return true;  // star: 中心-叶子有边
            }
            internal_edge_false_cnt++;       // star 叶子间无边
            return false;      // star: 叶子间无边
        }
        else if(supertype==3)  // diamond
        {
            // diamond {u,v,x,y}: member0(begin)和member1(end)与任意成员有边
            // 只有 member2-member3 之间无边; 即 u 或 v 是 begin/end 就有边
            int begin = superNodes[SUPERU].beginnode;
            int end = superNodes[SUPERU].endnode;
            if(u==begin || u==end || v==begin || v==end) {
                internal_edge_true_cnt++;    // diamond 涉及 begin/end
                return true;
            }
            internal_edge_false_cnt++;       // diamond x-y 无边
            return false;      // diamond: member2-member3 (x-y) 无边
        }
        else // path: 0905 两层并一层 — pos 判定 |Δpos|==1 (相邻⟺真边, bk/ctr/usa 实测假边率0%弦0)
        {
            if (g_path_pos_ok) {
                if (queryPathPos((uint32_t)u, (uint32_t)v)) {
                    internal_edge_true_cnt++;    // path 相邻对真边
                    return true;
                }
                internal_edge_false_cnt++;
                return false;
            }
            // 回退: path_pos.txt 缺失时走原 O(N) 两层逻辑
            const vector<int>& pathNodes = superNodeMembers[SUPERU];
            for(size_t i = 0; i < pathNodes.size() - 1; i++)
            {
                if((pathNodes[i] == u && pathNodes[i+1] == v) ||
                   (pathNodes[i] == v && pathNodes[i+1] == u))
                {
                    if (binary_search(adjInternal[u].begin(), adjInternal[u].end(), v)) {
                        internal_edge_true_cnt++;    // path 相邻对真边
                        return true;
                    }
                    internal_edge_false_cnt++;       // path 相邻对假边
                    return false;
                }
            }
            internal_edge_false_cnt++;           // path 非相邻对
            return false;
        }
    }

    // ========== 外部边路径 ==========

    // ========== 外部边路径: GT + 二分查找 ==========
#ifndef DISABLE_GT
    bool gt_has_common = (gtCode[u] & gtCode[v]).any();
    if(gt_has_common)
    {
#ifndef NO_GT_STATS
        no_edge_and_gt_get++;   // GT 正确预判非边, 跳过二分查找
#endif
        return false;
    }
#endif
    // GT 关闭或 GT 未命中 → 二分查找
    {
        // 不能使用一半邻接表 直接看度数
        if(adjExternal[u].size()>adjExternal[v].size())   swap(u, v);

        bool has_edge = binary_search(adjExternal[u].begin(), adjExternal[u].end(), v);

        if (has_edge)
        {
#if !defined(DISABLE_GT) && !defined(NO_GT_STATS)
            have_edge_and_gt_noget++;  // GT 漏判 (实际有边, GT 未预判)
#endif
            return true;
        }
        else
        {
#if !defined(DISABLE_GT) && !defined(NO_GT_STATS)
            no_edge_but_gt_noget++;    // 非边且 GT 未命中
#endif
            return false;
        }
    }
}

/////////////////////// 内部辅助：把 QueryGraph 转成邻接列表 ///////////////////////
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
// 使用 main.cpp 中的 FindNEC 逻辑重写
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

    // 使用 main.cpp 中的等价性检查逻辑
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
        //already in a NEC node
        if(p[i] != i) {
            vector <int>::iterator Iter = vertexlist->begin() + i;
            //this is slow for vector, but this function is only called once.
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
    // 实现 main.cpp 中的第二类NEC处理逻辑
    
    // 获取组内顶点的标签（假设同组顶点标签相同）
    if (vertexlist->empty()) return;
    
    
    for(int i = 0; i < vertexlist->size(); ++i) {
        if((*vertexlist)[i] == -1) {
            continue;
        }
        
        vector <int> a;  // 存储当前NEC组
        vector <int> b;  // 存储对应索引
        int Vi = (*vertexlist)[i];
        a.push_back(Vi);
        b.push_back(i);
        
        // 查找Vi在当前组中是否有同标签的邻居
        int pos2 = -1;
        for (int k = 0; k < qadj[Vi].size(); k++) {
            int neighbor = qadj[Vi][k];
            // 检查邻居是否在当前组中
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
        
        // 检查其他顶点是否与Vi形成团
        for(int j = i + 1; j < vertexlist->size(); j++) {
            int Vj = (*vertexlist)[j];
            if(Vj == -1) {
                continue;
            }
            
            // 检查Vj是否在当前组中与Vi有连接
            bool connected_to_Vi = false;
            for (int neighbor : qadj[Vj]) {
                if (neighbor == Vi) {
                    connected_to_Vi = true;
                    break;
                }
            }
            
            if (!connected_to_Vi) continue;
            
            // 检查Vi和Vj是否与当前a中的所有顶点相连
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
                // 检查是否满足"exclusive equals"条件（相互之间有边且邻居结构相同）
                auto exclusive_equals = [&](int u, int v) -> bool {
                    const std::vector<int>& Nu = qadj[u];
                    const std::vector<int>& Nv = qadj[v];
                    
                    // 检查除彼此之外的邻居是否完全相同
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
        
        //find the maximal one
        (*NECV).push_back(a);
        int sizea = a.size();
        for(int nn = sizea - 1; nn >= 0; nn--) {
            (*vertexlist)[b[nn]] = -1;
        }
    }
    
    // 清理标记为-1的顶点
    vertexlist->erase(
        std::remove_if(vertexlist->begin(), vertexlist->end(), 
                      [](int x) { return x == -1; }), 
        vertexlist->end()
    );
}

// 如果主动选取一些分数更低  度数更少的中间节点  能够避免拆散度数比较大的nec组合 让总的nec更少？0428
// 完全按照 main.cpp 中 RewriteToNECTree 函数逻辑重写，包含层级信息维护
// 新增 层级索引表 后续会用到
static NECNode* buildNECTree(const QueryGraph &Q, int start_q, const std::vector<std::vector<int>> &qadj, 
                             std::vector<NECNode*> &all_necs, std::vector<NECNode*> &node2nec_out) {
    int qn = (int)Q.node_ids.size();
    
    // 初始化标记数组
    std::vector<bool> flag(qn, false);

    // 创建根节点
    NECNode* root = new NECNode();
    root->id = 0;
    root->vertex_set.push_back(start_q);
    root->start_idx = 0;
    root->end_idx = 0;
    root->parent = nullptr;
    
    all_necs.push_back(root);
    
    // 初始化 node2nec_out 映射
    node2nec_out.assign(qn, nullptr);
    node2nec_out[start_q] = root;
    
    // 标记已访问的顶点
    flag[start_q] = true;

    int currentS = -1;
    int currentE = -1;
    int nextS = 0;
    int nextE = 0;
    int childS = 0;
    int childE = 0;
    
    // 层级索引表，用于记录每层的NEC节点范围
    std::vector<std::pair<int, int>> level_index; // {start_index, end_index}
    level_index.push_back({0, 0}); // 根节点层
    
    int current_level = 0;
    
    while(nextE >= nextS) {
        currentS = nextS;
        currentE = nextE;
        nextS = currentE + 1;
        nextE = currentE;
        childS = currentE + 1;
        childE = currentE;
        
        // 记录当前层的范围？好像没被用到？
        if (current_level < (int)level_index.size()) {
            level_index[current_level] = {currentS, currentE};
        } else {
            level_index.push_back({currentS, currentE});
        }
        
        //i is the NEC node ID
        for(int i = currentS; i <= currentE; i++) {
            // 模拟 main.cpp 中的 C 变量 (labelVlist 类型)
            std::vector<std::pair<std::string, std::vector<int>>> C; // {label, vertices}
            
            int Size2 = all_necs[i]->vertex_set.size();
            for(int j = 0; j < Size2; j++) {
                int v = all_necs[i]->vertex_set[j];
                int Size3 = qadj[v].size();
                for(int k = 0; k < Size3; k++) {
                    int neighbor = qadj[v][k];
                    if(!flag[neighbor]) {
                        // 获取邻居的标签
                        // label 统一 "user" (原从 Q.node_labels 取, 已停用)
                        std::string label_str = "user";
                        
                        // 查找是否已有该标签
                        int pos = -1;
                        for(int c_idx = 0; c_idx < (int)C.size(); c_idx++) {
                            if(C[c_idx].first == label_str) {
                                pos = c_idx;
                                break;
                            }
                        }
                        
                        if(pos == -1) {
                            // 创建新标签组
                            std::pair<std::string, std::vector<int>> new_label_group;
                            new_label_group.first = label_str;
                            new_label_group.second.push_back(neighbor);
                            flag[neighbor] = true;
                            C.push_back(new_label_group);
                        } else {
                            // 检查该顶点是否已在该标签组中
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
                    
                    // 设置子节点范围
                    if(all_necs[i]->children.empty()) {
                        all_necs[i]->children.resize(Size4);
                    } else {
                        // 扩展孩子数组
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
                        // 正确设置子节点引用
                        all_necs[i]->children[all_necs[i]->children.size() - Size4 + k] = new_node;
                        
                        // 更新映射
                        for(int vertex : NECV[k]) {
                            node2nec_out[vertex] = new_node;
                        }
                    }
                }
            }
        }
        
        current_level++;
    }
    
    // 可选：打印层级信息用于调试
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

// =============================================================================
// END OF MODIFICATION: 超节点摘要优化 - 结构剪枝辅助函数
// =============================================================================

// 需要修改，只用度数剪枝，，不调用函数
// 加上超节点摘要的剪枝？
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
    // 1. 安全检查：空 VM 直接返回 false
    // ---------------------------------------------
    // 可能是不必要的检查
    explorecr_calls++;                           // 统计调用次数

    // 超时检查: 主动查时钟置位 g_timed_out (避免外层循环等不到 ExploreCR 返回)
    if (g_timeout_sec > 0 && g_timed_out == false) {
        if (std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
            g_timed_out = true;
        }
    }
    if (g_timed_out) return false;  // 已超时, 快速 unwind

    int q_rep = all_necs[u_prime]->vertex_set[0];
    // std::string q_label =  Q.node_labels[q_rep];   // label 已停用
    int q_features = qadj[q_rep].size();

    // =============================================================================
    // 优化：去掉多余的超节点筛选
    // 说明：subaa_execute中已经做了超节点标签和度数过滤
    // 这里VM中的节点都是已过滤过的，直接处理即可
    // =============================================================================

    // 直接遍历VM中的候选节点，不需要按超节点分组和再次过滤
    for (int v_prime : *VM) {
        // 超时检查 (主循环顶部, VM 巨大时也能跳出)
        if (g_timed_out) return false;
        if (visited[v_prime] == g_current_epoch || globalDegree[v_prime] < q_features) continue;
        // 这里保证了祖先数据不会重复进入后续childpool

        // 剪枝2: 超节点 md 剪枝已废弃 — single 空摘要会使条件恒真, 误剪全部 single 候选 (历史 bug, 0809 修复)
        // 剪枝3: 拓扑传播剪枝 (借鉴综述 generateCandidates)。
        // v_prime 的邻居里必须至少有一个度数 >= child NEC 度数需求的顶点,
        // 否则该 child 连第一个候选都找不到, v_prime 注定无法扩展 → 早停。
        // 无标签有效 (只查度数+邻接, 不查标签)。
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

        // 前面只是多了一层超节点开始
        // ---------------------------------------------
        // 若 NEC 有 children，构建邻居候选集合
        // ---------------------------------------------
        if (!all_necs[u_prime]->children.empty())
        {
            std::vector<std::pair<int, std::vector<int>>> childPools;
            childPools.reserve(all_necs[u_prime]->children.size());

            for (auto *child : all_necs[u_prime]->children)
            {
                int q_child = child->vertex_set[0];

                // 【优化】使用引用避免vector拷贝
                // 所有child共享相同的邻接表（无标签分组），直接引用adjall
                const std::vector<int>& cand = adjMerged[v_prime];

                // 【论文逻辑】保存所有child pool（即使为空）
                // 论文第7行：EXPLORECR(u'_c, adj(v', L(u'_c)), CR)
                // 直接传递adj，不检查是否为空
                childPools.emplace_back(child->id, cand);
            }

            if (!matched_children) {
                visited[v_prime] = 0; /* 递归回溯恢复: 同 epoch 内标为未访问 */
                continue;
            }

            // -------- sort children by candidate pool size (smallest first)
            // 但先过滤掉pool为空的child（论文中不存在这种情况，因为adj()不会为空）
            std::vector<std::pair<int, std::vector<int>>> nonEmptyPools;
            for (auto &p : childPools) {
                if (!p.second.empty()) {
                    nonEmptyPools.push_back(p);
                }
            }

            // 如果所有pool都为空，跳过当前v_prime
            if (nonEmptyPools.empty()) {
                visited[v_prime] = 0; /* 递归回溯恢复: 同 epoch 内标为未访问 */
                continue;
            }

            // 按pool大小排序（论文第6行：ordered by adj size）
            std::sort(nonEmptyPools.begin(), nonEmptyPools.end(),
                      [](auto &a, auto &b) { return a.second.size() < b.second.size(); });

            // -------- recursively explore each child (论文Algorithm 3第7行)
            for (size_t idx = 0; idx < nonEmptyPools.size(); idx++)
            {
                // 超时检查 (child 递归前, 避免卡在深层)
                if (g_timed_out) { matched_children = false; break; }
                int childNEC = nonEmptyPools[idx].first;
                auto &childVM = nonEmptyPools[idx].second;

                // 【论文第7行】EXPLORECR(u'_c, adj(v', L(u'_c)), CR)
                if (!ExploreCR(childNEC, &childVM, CR, v_prime, all_necs, qadj, Q, visited))
                {
                    // 【优化】失败时清除已添加的子节点CR（对应开源版本第622-625行）
                    for (size_t k = 0; k < idx; k++) {
                        ClearCR(nonEmptyPools[k].first, v_prime, CR);
                    }
                    // 第10-11行：matched := false; break;
                    matched_children = false;
                    break;
                }
            }

            if (!matched_children) {
                visited[v_prime] = 0; /* 递归回溯恢复: 同 epoch 内标为未访问 */
                continue;
            }
        }

        visited[v_prime] = 0; /* 递归回溯恢复: 同 epoch 内标为未访问 */

        // ---------------------------------------------
        // 3. 插入 CR
        // ---------------------------------------------
        int pos = -1;
        for (int i = 0; i < (int)CR->parent[u_prime].size(); i++) {
            if (CR->parent[u_prime][i] == v) {
                pos = i;
                break;
            }
        }

        if (pos == -1) {   // 新 parent entry
            CR->parent[u_prime].push_back(v);
            CR->CR[u_prime].push_back({ v_prime });
        } else {
            PureInsert(CR->CR[u_prime][pos], v_prime);
        }

        // ---------------------------------------------
        // 4. root NEC 检查数量 >= NEC 节点数（提前返回优化）
        // ---------------------------------------------
        if (v == -1) {
            // 找到parent=-1的条目位置
            int pos = -1;
            for (int i = 0; i < (int)CR->parent[u_prime].size(); i++) {
                if (CR->parent[u_prime][i] == -1) {
                    pos = i;
                    break;
                }
            }
            // 如果parent=-1的条目已经有足够候选，可以提前返回
            if (pos != -1 && (int)CR->CR[u_prime][pos].size() >= (int)all_necs[u_prime]->vertex_set.size())
            {
                return true;  // 论文Algorithm 3第19行：return DONE
            }
        }
    } // end for v_prime in *VM

    // =============================================================================
    // END OF MODIFICATION
    // =============================================================================

    // ---------------------------------------------
    // 5. 非 root NEC，parent entry 不存在 => false
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
    
    // root NEC (v=-1) 默认返回true
    // 【论文Algorithm 3第19行】return DONE
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

// =============================================================================
// Efficient combination iterator - similar to std::next_permutation
// =============================================================================

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
        // Initialize first combination {0, 1, ..., k-1}
        for (int i = 0; i < k; ++i) {
            indices[i] = i;
        }
        // Skip non-clique combinations if needed
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
 * @brief 计算组合数 C(n, k) 的辅助函数
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

    // 如果是钻石 最后一层是1 2 的交集而且减去0使用掉的一个共同邻居1给。addsize=1 但是注意是要先算pool size 等于交集 CR
    // 暂时不考虑标签 后续的可以是标签邻接表分组 如果0的标签和3不同当然不用减去
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

    // DFS 超时检查 (节流: 每 8192 次递归查一次时钟, 避免 steady_clock 开销)
    {
        static long long _dfs_check_cnt = 0;
        if (g_timeout_sec > 0 && ((++_dfs_check_cnt) & 1023) == 0) {
            if (std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
                g_timed_out = true;
            }
        }
        if (g_timed_out) return;
    }

    // // q5
    //  // DFS终止条件：当depth达到order.size()时计数匹配
    //     // 虚假保存：只增加 size，不保存实际匹配
    //     // outMatches.emplace_back();  // 添加空的 map
    //     current_match_cnt++;

    int u_prime = order[depth];//当前nec id
    NECNode* nec = all_necs[u_prime];
    const std::vector<int> &qGroup = nec->vertex_set;//当前nec的query节点集合？
    int k = static_cast<int>(qGroup.size());

    // ========== 阶段1: 获取候选池 ==========

    // 获取候选池
    std::vector<int> pool;
    // 父数据节点数组（供isJoinable使用）
    std::vector<int> parent_data_nodes;

    int parent_node_id = all_necs[u_prime]->parent ? all_necs[u_prime]->parent->id : -1;

        // 非根节点：获取候选池
        NECNode* parent_nec = all_necs[parent_node_id];
        const std::vector<int>& parent_query_nodes = parent_nec->vertex_set;
        int parenr_node_data = M[parent_query_nodes[0]];// 父NEC的第一个查询节点对应的数据节点（假设父NEC内部成团）

    //     // 有快速计数

        // =============================================================================
        // 自适应策略：根据父NEC的大小选择最优方法
        // 关键洞察：父NEC内部是否成团对子NEC没有影响，只需要考虑父NEC的大小
        // =============================================================================

    if (parent_query_nodes.size() == 1) {
            // ================================================================
            // 快速路径：父NEC只有1个查询节点
            // 直接使用该节点的CR条目
            // ================================================================
            int parent_q = parent_query_nodes[0];
            int parent_dn = M[parent_q];

            if (parent_dn == -1) {
                return;
            }

            // 查找CR条目
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

            // 直接复制CR条目到pool（避免引用被修改）
            pool = CR->CR[u_prime][pos];

            // 保存父数据节点
            parent_data_nodes.push_back(parent_dn);
    }
    else {
            // ================================================================
            // 多父节点路径：父NEC有多个查询节点
            // 策略：计算所有父节点的CR条目交集
            // ================================================================

            // 获取所有父数据节点
            parent_data_nodes.reserve(parent_query_nodes.size());
            for (int parent_q : parent_query_nodes) {
                int dn = M[parent_q];
                if (dn == -1) {
                    return;  // 父节点未映射，无法继续
                }
                parent_data_nodes.push_back(dn);
            }

            // 查找所有父节点的CR条目
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
                    return;  // 找不到CR条目
                }
                cr_entries.push_back(&CR->CR[u_prime][pos]);
            }

            // 计算交集（使用std::set_intersection的优化版本）
            // 假设CR条目已排序
            std::vector<int> C1 = *cr_entries[0];  // 从第一个开始

            for (size_t i = 1; i < cr_entries.size(); ++i) {
                const std::vector<int>* next_cr = cr_entries[i];
                std::vector<int> intersection;
                intersection.reserve(std::min(C1.size(), next_cr->size()));

                // 双指针交集计算（O(n + m)）
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
                    return;  // 交集太小，无法组成NEC
                }
            }

            pool = std::move(C1);
    }

    // 如果候选节点池为空，直接返回（无法继续匹配）
    if (pool.empty()) {
        return;
    }

    // ========== 阶段3: 计算需要检查的邻居 ========== 放在前面 因为不需要F 而且倒数二层快速isjoin检查需要这neighbors_to_check

    // 【优化】预计算：为qGroup计算需要检查连接性的数据节点
    // 避免在isJoinable中重复查找M[qnbr]
    // 注意：qGroup中的节点都属于同一个NEC，它们的邻居检查列表相同，所以只需要计算一次
    std::vector<int> neighbors_to_check;
    // neighbors_to_check.reserve(qadj[qGroup[0]].size());  // 使用第一个节点的邻居数预估
    // 不要预估 可能没有

    // 只需要用qGroup中任意一个节点来计算（它们邻居结构相同）
    int q = qGroup[0];
    for (int qnbr : qadj[q]) {//这个当前qgroup的邻居可能是同一层nec 所以要进行dn!=-1的过滤
        int dn = M[qnbr];
        if (dn == -1) continue;

        // 检查是否是父数据节点，如果是则跳过
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
    // 在这里可以进行一个GT | 上的计算，如果这些数据里面有来自相同超节点的 取其中一个即可。 需要修改
        //在这里进行最后层之前4-5的快速计数：结合前面的过滤问题 甚至可以在更前面进行复核的F和isjpin过滤 前提是有neighbors_to_check
    
    
    
    if(depth == static_cast<int>(order.size()) - 1)
    {
        // [B0] 悬挂点叶子 O(1) 快速计数（泛化自 q6 硬编码，零查询特定常量）
        // 触发条件: k==1（叶子 NEC 只含 1 个查询节点）且该叶子查询节点在 qadj 中
        //           只有 1 个邻居（即悬挂点：叶子—父，无其他查询边）。
        //           q6 原版写死 u_prime==4，这里用 qadj[qGroup[0]].size()==1 等价判定。
        // 思路: 叶子的合法候选 = 父数据节点 m_parent 的邻居里，还没被占用的节点数。
        //       m_parent 总邻居数 = globalDegree[m_parent]，扣掉两类占用：
        //         (a) query_nbr_occupied: 父查询节点的查询邻居（除叶子外）都已匹配，
        //             子图同构保证它们的数据映射都是 m_parent 的邻居，必然占用。
        //             数量 = qadj[q_parent].size() - 1（减叶子自己那条边）。
        //             （对应 q6 原版的 "-3"：qadj[4]={0,1,3,5}, 4-1=3）
        //         (b) extra_occupied: 与父查询无边、但数据上恰好与 m_parent 有边、
        //             且已匹配的查询节点。这种占用不是查询边，(a) 没扣到，这里补扣。
        //             （对应 q6 原版的 "if has_edge_in_data(m4,m2) addsize--"：
        //              节点2 不是4的查询邻居，但数据上 M[2] 可能与 m4 相邻）
        if (k == 1 && qadj[qGroup[0]].size() == 1) {
            int q_leaf = qGroup[0];
            int q_parent = qadj[q_leaf][0];          // 叶子的唯一查询邻居 = 父
            int m_parent = M[q_parent];
            // (a) 查询邻居占用
            int query_nbr_occupied = (int)qadj[q_parent].size() - 1;
            // (b) 额外占用：遍历所有已匹配查询节点，挑出"非查询邻居但数据有边"的
            int extra_occupied = 0;
            const std::vector<int>& nbrs_of_parent = qadj[q_parent];  // 有序（buildQueryAdj 排过序）
            int qn = (int)qadj.size();
            for (int qd = 0; qd < qn; ++qd) {
                if (qd == q_parent || qd == q_leaf) continue;
                int md = M[qd];
                if (md == -1) continue;  // 未匹配跳过（M 初始化为 -1）
                // 二分判定 qd 是否为 q_parent 的查询邻居：
                //   是 → 属于 (a)，已被 query_nbr_occupied 扣过，跳过；
                //   否 → 才查数据边，若 M[qd] 与 m_parent 相邻则算额外占用。
                // 用 binary_search 是因为 nbrs_of_parent 已有序，O(log deg) 最快。
                if (std::binary_search(nbrs_of_parent.begin(), nbrs_of_parent.end(), qd)) continue;
                if (has_edge_in_data(md, m_parent)) extra_occupied++;
            }
            int addsize = globalDegree[m_parent] - query_nbr_occupied - extra_occupied;
            if (addsize < 0) addsize = 0;  // 防御（理论上不会负）
            current_match_cnt += addsize;
            if (current_match_cnt >= matchtarget) return;
            return;
        }
        // q2快速计数器 — 判断 NEC 是否成团: 成团走 GT 建邻接数 clique，否则 pool 逐边计数
        bool necIsClique = (k >= 2) &&
            std::binary_search(qadj[qGroup[0]].begin(), qadj[qGroup[0]].end(), qGroup[1]);
        if(!necIsClique)
        {
            long long pooljoinablecnt = 0;
            if(neighbors_to_check.size()==1)//看似泛用的避免多调用一次isjoin的办法，在q2中进入size=1分支
            {
                
                int parent_need_check_joinable = neighbors_to_check[0];
                for(int node:pool)
                {
                    if (g_timed_out) return;
                    if(F[node]!=g_f_epoch)//放弃使用 用isjoin
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
                    if(F[node]!=g_f_epoch && isJoinable_usinghas_edge_in_data(node, neighbors_to_check))//更严谨 不需要写死M3 HASEDGE{
                    {
                        pooljoinablecnt++;
                    }
                }

            }
            current_match_cnt += pernum_A_M_N(pooljoinablecnt, qGroup.size());
            // current_match_cnt += pooljoinablecnt;
            // +=AN NECSIZELAST ,这里AN  1= N

        }
        // // k一定大于1，而且需要检查成团条件 搬运q0的分支 集成到最后一层快速计数器
        else {
            // 先过滤掉不满足 isJoinable 条件的节点
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
                for (int i = 0; i < psz; ++i) kc += (int)nb[i].size();  // 每条边计一次
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
                // k >= 4: 兜底分支，使用 ComboIterator 逐个组合计数
                // 不保证最快，但通用且正确
                // 注意：这里复用 filtered_pool（已过滤 isJoinable），ComboIterator 的 needClique=true
                // 会检查内部边，但由于是 clique NEC，理论上所有组合都应该是 clique
                ComboIterator comboIter(filtered_pool, k, true);  // needClique=true
                if (!comboIter.finished) {
                    do {
                        // comboIter 已经确保是 clique（needClique=true）
                        // 每个 combo 对应 k! 种排列
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
        // 单节点NEC优化：跳过组合和排列生成，直接遍历pool
        // C(n,1)=n，不需要组合迭代器
        // 单元素只有一种排列，不需要next_permutation
        // const auto& neighbors_to_check_ref = neighbors_to_check;
        for (int node : pool)
        {
            // 超时检查 (pool 可能巨大, 单次循环也会卡, 需在循环内检查)
            if (g_timed_out) return;
            // F过滤
                        // F过滤
            if(F[node]==g_f_epoch)
            {
                continue;
            }

            // ========== 阶段4: Joinable检查 ==========
            // Check if joinable with existing neighbors
            if (!isJoinable_usinghas_edge_in_data(node, neighbors_to_check)) {
                continue;
            }

            // ========== 阶段5: 更新状态 ==========
            // UpdateState(M, F, qGroup, single_node_combo);
            F[node] = g_f_epoch;
            M[qGroup[0]] = node;

            // ========== 阶段6: 递归调用 ==========
            subgraphSearch_TurboIso_rec(order, qadj, all_necs, node2nec,
                                      M, F, CR, outMatches, depth + 1);

            // 一旦达到目标直接返回，不用恢复状态了
            if (current_match_cnt >= matchtarget) {
                return;
            }

            // ========== 阶段7: 恢复状态 ==========
            // RestoreState(M, F, qGroup, single_node_combo);
            F[node] = 0;
            M[qGroup[0]] = -1;
        }
        return;  // k=1处理完毕，直接返回
    }

    // 下面是k>=2的情况
    // ========== 阶段2: F过滤 ========== 现在放在阶段3之后 阶段3不需要2 而且前面需要有倒数第层快速计数 连带使用F过滤

    // 【优化】对pool进行F过滤，移除已使用的节点
    // 这样后续组合生成时就不需要重复检查F
    size_t write_idx = 0;
    for (size_t read_idx = 0; read_idx < pool.size(); ++read_idx) {
        if (g_timed_out) return;
        if (F[pool[read_idx]]!=g_f_epoch) {
            pool[write_idx++] = pool[read_idx];
        }
    }
    pool.resize(write_idx);

    // 如果过滤后pool为空，直接返回
    if (pool.empty()) {
        return;
    }

   
    // 这里不用检查size了 前面已经有k验证了，其实对于不同的起点开始进行DFS 的每一层都要算一次isclique 可以提前计算，存一个数组
        int memberA = qGroup[0];
        int memberB = qGroup[1];
        bool necIsClique = std::binary_search(qadj[memberA].begin(), qadj[memberA].end(), memberB);

    // ========== 阶段4: 组合生成 ==========
    // Use efficient combination iterator (replaces batch generation)
    ComboIterator comboIter(pool, k, necIsClique);

    // 【BUG FIX】while(comboIter.next()) 跳过第一个组合 → 改 do-while
    if (!comboIter.finished) {
        do {
            const auto& combo = comboIter.getCombo();
        // F过滤已在pool生成后完成，这里无需再检查

        // ========== 阶段5: Joinable检查 ==========
        // Check if joinable with existing neighbors
        bool ok = true;
        for (size_t i = 0; i < qGroup.size(); ++i) {
            if (!isJoinable_usinghas_edge_in_data(combo[i], neighbors_to_check)) {
                ok = false;
                break;
            }
        }

        if (!ok) continue;

        
         // ========== 阶段6: 递归调用和排列生成 ==========
        // Use next_permutation to generate permutations iteratively
        std::vector<int> perm = combo;
        long long match_cnt_before = current_match_cnt;
        do {
            //     current_match_cnt += delta;
            //     // continue;//可能多个combo? 或者直接加剩余排列数乘delta个 先测试
            //     // 现在全局计数器更新不对导致又开了一个root
            // combofirstperm = false;// 第一次更新
            // ========== 阶段6: 更新状态 ==========
            UpdateState(M, F, qGroup, perm);

            // ========== 阶段7: 递归调用 ==========
            subgraphSearch_TurboIso_rec(order, qadj, all_necs, node2nec,
                                      M, F, CR, outMatches, depth + 1);

            // 一旦达到直接先返回 不用恢复状态了
            if (current_match_cnt >= matchtarget) {
                return;
            }

            // ========== 阶段8: 恢复状态 ==========
            RestoreState(M, F, qGroup, perm);

            long long delta = current_match_cnt - match_cnt_before;
            // current_match_cnt += delta*(k全排列数-1);
            current_match_cnt += delta;
            break;

        } while (std::next_permutation(perm.begin(), perm.end()));
        } while (comboIter.next());
    } // if (!comboIter.finished)
}

// TurboISO 起点选择 (原版保留)
int chooseStartQVertex_TurboIso_runtime(
    const QueryGraph &Q)
{
    using namespace std::chrono;
    auto t0 = high_resolution_clock::now();

    cout << "\n=== [TurboIso] Stage: ChooseStartQVertex ===" << endl;
    // -------------------------------------------------------
    // Step 1. Scan all superNodes to count data label frequency
    // -------------------------------------------------------
    // label 已停用 (统一 "user"): 原 labelFreq 扫描段注释
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

// 暂时没问题 不使用

Matches runMatch(const QueryGraph &Q) {
    cout << "--------------------runMatch" << endl;
    g_timed_out = false;
    // SIGALRM 已在 main 注册, 此处只重置计时起点 (让超时从匹配开始算)
    // 注意: 若想从 main 起算, 注释掉下面这行
    // g_subac_t0 = std::chrono::steady_clock::now();
            
    Matches results;

    // 1) build qadj
    std::vector<std::vector<int>> qadj;
    buildQueryAdj(Q, qadj);
    int qn = (int)Q.node_ids.size();
    if (!qn) return results;

    // 前面的也没有占用时间
    // 4) build NEC tree

    std::vector<NECNode*> all_necs;
    std::vector<NECNode*> node2nec; // size qn, node2nec[qi] -> NECNode*
    int start_q = chooseStartQVertex_TurboIso_runtime(Q);
    
    // 新版本 不访问超节点 按照论文rankU公式估计
    NECNode* nec_root = buildNECTree(Q, start_q, qadj, all_necs, node2nec);

    // 打印查询树
    std::cout << "=== NEC Tree Structure ===" << std::endl;

    // 下面的到for循环前面 不占时间

    // 5) init candidates
    std::vector<std::vector<int>> candidates;
    // 这里应该初步过滤一些候选节点
    
    // 这玩意不知道干什么的 是不是顶点你就全部加入？不如explore

    // 遍历所有数据顶点作为候选区域的起始点
    // 这里可以先遍历超节点初步选择,1027new md正确
    int qStartId=nec_root->vertex_set[0];
    int qStartDegree=qadj[qStartId].size();

    // 准备一个NEC0是否排斥pathmiddlenode
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
            excludePathMidNode=true;//形成三角形，而pathmiddlenode 不应该在三角形中
        }
    }

    // 要查询的顶点放在这儿
    int root_query_vertex = qStartId;
   
    // std::string root_label = Q.node_labels[root_query_vertex];   // label 已停用

    // ========== 0903: 直接使用固定 NEC 匹配顺序 (g_fixed_nec_order), 不再计算 ==========
    // NEC 结构与 order 只依赖查询图拓扑, 每个 query 只有一个 order, 见全局表注释。
    auto fit = g_fixed_nec_order.find(g_query_id);
    if (fit == g_fixed_nec_order.end()) {
        std::cerr << "[ERROR] q" << g_query_id << " 没有固定 NEC order" << std::endl;
        std::exit(1);
    }
    std::vector<int> order = fit->second;
    std::cout << "[Fixed NEC Order] q" << g_query_id << " order:";
    for (size_t _i = 0; _i < order.size(); ++_i) std::cout << " " << order[_i];
    std::cout << std::endl;

    // auto exploreCR_start = std::chrono::steady_clock::now();
    // 测试下面这个循环的时间

    // visited数组在循环外定义 (版本号方案: int 数组, 用 current_epoch 判断, 避免全量 assign)
    std::vector<int> visited;
    visited.assign(numOriNodes, 0);  // 只在循环外初始化一次, 循环内不再重置

    // 【优化6】预分配M和F向量
    std::vector<int> M;
    M.reserve(qn);  // 预留空间
    std::vector<int> F;
    F.assign(numOriNodes, 0);  // 版本号方案: 循环外初始化一次, 循环内用 g_f_epoch++

    for (size_t super_idx = 0; super_idx < super_order.size(); ++super_idx) {
        int super_id = super_order[super_idx];  // 按排序后的顺序遍历
        // ===== 超时检查（外层）+ 心跳 (诊断卡在循环哪轮) =====
        if (g_timeout_sec > 0 &&
            std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
            g_timed_out = true;
            break;
        }
        // 这里就是取用超节点了
        // ===== 0809 single 守卫:single 超节点(id >= firstSingleId)无摘要, 按原节点语义处理 =====
        //   md   → 用唯一成员的 globalDegree 替代(注意: 是成员节点, 不是 super_id)
        //   type → 恒为 single(不是 path, 走 else 取全部成员)
        //   候选 → superNodeMembers 不压缩, single 区照常返回单元素
        bool is_single = (super_id >= firstSingleId);

        // --- md 粗剪枝 ---
        if (is_single) {
            int member = superNodeMembers[super_id][0];   // single 唯一成员(不是 super_id 本身)
            if (globalDegree[member] < qStartDegree) continue;
        } else {
            if (superNodes[super_id].md < qStartDegree) continue;
        }

        // // label cut  ignore label

        vector<int>validDataNodes;
        // 这里想让path的中间节点不当顶点
        // path middle node cut if flg
        // single 不可能进 path 分支(!is_single 短路守卫, 避免 single 读空摘要)
        if(!is_single && excludePathMidNode && superNodes[super_id].type==4)  // path
        {
            // 上游问题(非下游): ctr 实测(adj1验证) 当前 synopsis 的 begin=链尾邻居(度2)、
            // end=链尾, begin 不是链头(members front 才是)。待收缩侧修正后此处自然恢复正确。
            // 下游按"摘要自包含"设计, 坚持读 synopsis, 不退回 members 推导。
            validDataNodes.push_back(superNodes[super_id].beginnode);
            validDataNodes.push_back(superNodes[super_id].endnode);
            // 只加入path的起始和结束节点 其他的都不加入，形状剪枝
        }
        // 这里也不对因为snodes里面只有path有
        else
        {
            validDataNodes = superNodeMembers[super_id];
        }

        // --- 构造局部候选节点集合 ---
        for (int start_data_vertex : validDataNodes) {//超节点顺序
            // ===== 超时检查（内层）+ 心跳 =====
            if (g_timeout_sec > 0 &&
                std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
                g_timed_out = true;
                break;
            }
            // 检查起始数据顶点的标签是否与根NEC节点的标签匹配
            // 检查具体的实际点 start_data_vertex是否具有对应的标签 label ignore
            
        
            // 初始化候选区域树(CR)
            CRTree CR;
            CR.init((int)all_necs.size());

            // 每次循环重新创建VM（更安全，避免预设大小不足）
            std::vector<int> VM({start_data_vertex});

            // 超时检查 (assign 前, 避免卡在 assign 上 ExploreCR 入口检查不到)
            if (g_timeout_sec > 0 && g_timed_out == false) {
                if (std::chrono::steady_clock::now() - g_subac_t0 > std::chrono::seconds(g_timeout_sec)) {
                    g_timed_out = true;
                }
            }
            if (g_timed_out) break;

            // 版本号方案: 递增 epoch 即让旧 visited 值全部失效 (替代 visited.assign 全量重置)
            g_current_epoch++;

            // // ExploreCR 计时
            auto exploreCR_start11 = std::chrono::steady_clock::now();
            bool explore_success = ExploreCR(0, &VM, &CR, -1, all_necs, qadj, Q, visited);// 在这里 开始节点start_data_vertex的度数检查放在exploreCR里面 不用提前做
            auto exploreCR_end11 = std::chrono::steady_clock::now();
            std::chrono::duration<double> exploreCR_duration = exploreCR_end11 - exploreCR_start11;
            explorecr_time_ms += exploreCR_duration.count() * 1000.0;
            exploreCR_call_count++;
            //start_data_vertex: 7448 explore CR time: 2257 ms

            if (explore_success) {

                // 不同起点使用新的M F
                // 【优化6】重用预分配的M和F向量
                M.assign(qn, -1);  // 重置为-1
                g_f_epoch++; /* 替代 F.assign 全量重置 */

                // 初始化映射状态：将根查询节点映射到起始数据顶点
                int root_query_vertex = nec_root->vertex_set[0];

                M[root_query_vertex] = start_data_vertex;
                F[start_data_vertex] = g_f_epoch;
                
                // 执行子图搜索 (直接传递 CR 树，不使用 candidates)
                // 这里的1是depth但是实际从0还是从1开始？ 0

                // ========== 总DFS时间计时（包含所有深度累积） ==========
                auto dfs_total_start = std::chrono::steady_clock::now();

                subgraphSearch_TurboIso_rec(order, qadj, all_necs, node2nec,
                            M, F, &CR, results, 1);

                auto dfs_total_end = std::chrono::steady_clock::now();
                dfs_total_time_all_ns += std::chrono::duration_cast<std::chrono::nanoseconds>(dfs_total_end - dfs_total_start).count();

                if(current_match_cnt >= matchtarget)
                {
                    //性能输出 省略
                    for (NECNode* n : all_necs) delete n;
                    return results;
                }
                M[root_query_vertex] = -1;
                F[start_data_vertex] = 0;

                // 清理CR资源?
                // CR析构函数会自动清理
            }
        }//end for node v
    }//end for supernode //可以考虑直接遍历实际节点 就不会先进行很大的exploreCR explore时间减少但是DFSsearch会增多

    for (NECNode* n : all_necs) delete n;

    if (g_timed_out) {
        std::cout << "[TIMEOUT] reached g_timeout_sec=" << g_timeout_sec
                  << "s, current_match_cnt=" << current_match_cnt << std::endl;
        // ===== 分阶段时间诊断 (超时也要打印, 看卡在哪) =====
        std::cout << "\n========== STAGE BREAKDOWN (timeout) ==========" << std::endl;
        std::cout << "ExploreCR total time: " << (explorecr_time_ms / 1000.0) << " s" << std::endl;
        std::cout << "DFS (subgraphSearch) total time: " << (dfs_total_time_all_ns / 1e9) << " s" << std::endl;
        std::cout << "ExploreCR outer calls (per start_data_vertex): " << exploreCR_call_count << std::endl;
        std::cout << "ExploreCR recursive calls: " << explorecr_calls << std::endl;
        std::cout << "has_edge_in_data total calls: " << has_edge_in_data_total_calls << std::endl;
    }
    // 不用返回了 只保留计数
    return results;
}

// 加载函数 不用修改
// ------------------------------读取和预先建立函数-------------------

// 加载函数结束
// 生成查询图和打印结果函数
// 中等规模团查询图 (5-7个节点的完全图)

// ========== 0804 合并版: 六合一查询图 dispatcher ==========
// 用 qid 选择查询图, 避免维护六个 cpp。
// 图结构来自各 q 文件的 create_test_query_* 函数, 已逐个核对一致。
// qid | 节点数 | 边数 | 结构简述
// ----|--------|------|----------
//  0  |   4    |  6   | 4-clique (完全图 K4)
//  2  |   4    |  5   | 钻石图 (K4 去掉一条边)
//  3  |   5    |  6   | 5-环 + 一条弦 (0-2)
//  5  |   5    |  8   | 5 节点特殊图 (度 0=2,1=4,2=4,3=3,4=3)
//  6  |   6    |  9   | 6-环 + 三条弦 (0-2,0-3,0-4)
//  7  |   6    |  9   | 6 节点 9 边 (含悬挂点 5-4)
// ========================================================
QueryGraph create_query(int qid) {
    QueryGraph Q;

    auto add_nodes = [&](int n) {
        for (int i = 0; i < n; ++i) {
            Q.node_ids.push_back(i);
            // Q.node_labels.push_back("user");   // label 已停用 (无标签场景)
        }
    };

    switch (qid) {
        case 1: {
            // 4-clique: 完全图 K4 (来自 q1 create_test_query_clique(4))
            add_nodes(4);
            for (int i = 0; i < 4; ++i)
                for (int j = i + 1; j < 4; ++j)
                    Q.edges.push_back({i, j});
            // 兼容 q1 的 pernumk 赋值 (clique_size-1 的阶乘, 4-clique 时 = 3! = 6)
            pernumk = 1;
            for (int i = 2; i <= 3; ++i) pernumk *= i;
            break;
        }
        case 2: {
            // 钻石图 (来自 q2): K4 去掉边 1-3
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
            // 5-环 + 弦 0-2 (来自 q3): 环 0-1-2-3-4-0
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
            // 5 节点特殊图 (来自 q4): 度 0=2,1=4,2=4,3=3,4=3
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
            // 6-环 + 三条弦 0-2,0-3,0-4 (来自 q5): 环 0-1-2-3-4-5-0
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
            // q6 图 (来自 q6): 6 节点 9 边, 含悬挂点 5-4
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
            std::cerr << "[ERROR] 未知 qid=" << qid
                      << " (支持: 1 2 3 4 5 6)" << std::endl;
            std::exit(1);
    }

    return Q;
}

void print_matches(const vector<unordered_map<int, int>>& all_matches) {
    cout << "Found " << current_match_cnt << " matches:" << endl;
}

int main(int argc, char** argv) {
    // 旧版: 第三个参数为查询图编号 qid (1 2 3 4 5 6, 连续编号)
    // 第 4 参可选 trial_count (默认 1, 脚本 TRIALS 传入)
    if (argc < 4) {
        cout << "Usage: " << argv[0] << " <match_target> <dataset_name> <query_id> [trial_count]" << endl;
        cout << "       query_id 支持: 1 2 3 4 5 6" << endl;
        return 1;
    }

    // 获取数据集名称与查询编号
    string dataset_name = argv[2];
    int query_id = std::stoi(argv[3]);
    g_query_id = query_id;  // 供 runMatch 内查询特定逻辑 (如 q2 手动 order 覆盖) 使用

    // 可选第 4 参 = 重复次数 (trial_count), 默认 1
    int trial_count = 1;
    if (argc >= 5) {
        trial_count = std::max(1, std::atoi(argv[4]));
    }

    // 在 main 开头注册 SIGALRM 软超时 (覆盖图加载 + 匹配全过程)
    // 无论卡在哪, 到点 alarm_handler 都会打印 CR/DFS 累计时间后退出
    g_subac_t0 = std::chrono::steady_clock::now();
    if (g_timeout_sec > 0) {
        signal(SIGALRM, alarm_handler);
        alarm((unsigned)g_timeout_sec);
    }

    // 构建带数据集名称的路径前缀
    string output_prefix = "../data/output_" + dataset_name + "/";
    string gtbin_path = "../data/gtbin/" + dataset_name + ".bin";

    cout << "Dataset: " << dataset_name << endl;

    //通用数据加载部分---------------------------------
    readSummaryStats((output_prefix + "summary.txt").c_str());
    // 算法计时,这里可以传入一个超级图的总数
    cout<<"From summary.txt: numRemainNodes: "<<numRemainNodes<<", supernodenum: "<<supernodenum<<", numOriNodes: "<<numOriNodes<<endl;
    // 这里是fc'
    cout<<"numRemainNodes: "<<numRemainNodes<<endl;
    cout<<"numOriNodes: "<<numOriNodes<<endl;
    adjExternal.resize(numOriNodes);
    cout<<"adjExternal size: "<<adjExternal.size()<<endl;
    cout<<"supernodenum: "<<supernodenum<<endl;

    // 这里只是新旧节点的映射关系idmap2，用来把新编号TG对应到旧编号上
    loadIdMapping((output_prefix + "id_mapping.txt").c_str());

    superNodeMembers.resize(supernodenum);
    // // 这是建立超图邻居表的，所以只要超图连续，那么预设成超节点个数就可以了 这里用不上
    superAdj.resize(supernodenum);
    // // 建立FC关系以及FC'关系，便于得到实际节点对应的超节点和超节点对应的实际节点集合
    // // 这个三角形查询也暂时用不上
    loadMappingTxt((output_prefix + "mapping.txt").c_str());   // 只填 nodeToSuper
    loadMembersTxt((output_prefix + "members.txt").c_str());   // fC' 填 superNodeMembers(保序, 含single)
    // //这个加载的是旧节点，没有剔除内部点

    // 全局度数
    globalDegree.clear();
    globalDegree.resize(numOriNodes);

 
    // 实际图内部邻居邻接表
    loadExternalAdj((output_prefix + "external_adj.txt").c_str());

    // 实际图GT
    loadBitCodesBinary(gtbin_path.c_str());

    // 新增 读取摘要 supernodes是摘要结构体vector
    // 里面nodes和从mapping那边得到顺序不一样 暂时用syn里面的覆盖
    superNodes = loadSuperGraph((output_prefix + "synopsis.txt").c_str());

    // 【超节点排序器】type反序(single<path<star<clique) + md降序。
    // 不重排 superNodes 数组(会破坏 nodeToSuper/superNodeMembers 索引), 只建索引顺序。
    super_order.resize(superNodes.size());
    for (size_t i = 0; i < superNodes.size(); ++i) super_order[i] = (int)i;

    // ========== 超节点排序器 (切换版本时改 type_rank 的 return 和 md 比较方向) ==========
    // 当前生效: ① clique优先 + md升序
    // single 区 (id >= firstSingleId) 无摘要, 不读 sa.type/sa.md;
    //       single 恒排末尾 (rank=3), single 之间按 id 升序保持稳定。
    auto type_rank = [](uint8_t t) -> int {
        // clique 优先 (clique<star<path<single); t: 1=clique 2=star 3=diamond 4=path 5=single
        if (t == 1) return 0;  // clique
        if (t == 2) return 1;  // star
        if (t == 3) return 2;  // diamond (与 path 同档或紧邻, 保持非 single 即可)
        if (t == 4) return 2;  // path
        return 3;  // single(5) 或其他 → 末尾
    };
    std::sort(super_order.begin(), super_order.end(), [&](int a, int b) {
        bool a_single = (a >= firstSingleId);
        bool b_single = (b >= firstSingleId);
        if (a_single && b_single) return a < b;   // 两个 single: 按 id 升序
        if (a_single) return false;                // a 是 single, b 不是 → a 排后
        if (b_single) return true;                 // b 是 single, a 不是 → a 排前
        const SuperNode& sa = superNodes[a];       // 双方都非 single: 摘要安全
        const SuperNode& sb = superNodes[b];
        int ra = type_rank(sa.type), rb = type_rank(sb.type);
        if (ra != rb) return ra < rb;
        return sa.md < sb.md;  // md 升序
    });

    // ----- 其他版本 (注释保留, 切换时替换上面 type_rank + md 比较即可) -----
    // ② clique优先 + md降序:
    //   type_rank: clique=0, star=1, path=2, single=3
    //   md 比较: return sa.md > sb.md;
    // ③ clique最后 + md升序:
    //   type_rank: single=0, path=1, star=2, clique=3
    //   md 比较: return sa.md < sb.md;
    // ④ clique最后 + md降序 (q6 通吃最优):
    //   type_rank: single=0, path=1, star=2, clique=3
    //   md 比较: return sa.md > sb.md;
    // 通用数据加载部分-------------------------------------------------

    // 新增 这个算法需要内部邻接表
    // 内部邻居邻接表，会用到这部分度数和邻居，因为会遍历一个点的所有邻居和度数剪枝
	adjInternal.resize(numOriNodes); // 确保有足够的空间
	loadInternalAdj((output_prefix + "internal_adj.txt").c_str());

	// path 内部 O(1) 查边结构 (缺失则 g_path_pos_ok=false, path 分支回退 O(N))
	loadPathPos(output_prefix + "path_pos.txt");

	// 【Bug 2 修复】排序 adjInternal/adjExternal 以便 binary_search 验证 clique 边
	for (int i = 0; i < numOriNodes; ++i) {
	    sort(adjInternal[i].begin(), adjInternal[i].end());
	    sort(adjExternal[i].begin(), adjExternal[i].end());
	}

	// 【新增】构建完整邻接表adjall：合并adj1和adj2
	mergeAdjacentLists();

    // 标签，先手动生成全是user，查询也是
    // 在这里把dataNodeLabel的值全部变成user,size是 实际 数据点的size 从0开始
    dataNodeLabel.resize(numOriNodes);
    for(int i = 0; i < numOriNodes; i++) {
        dataNodeLabel[i] = "user";
    }

    cout << "=== SubA<sub>A</sub> Algorithm Demo ===" << endl;

    // 设置查询数量和查询图
    matchtarget= std::stoi(argv[1]);
    QueryGraph Q = create_query(query_id);

    size_t gt_memory_footprint = gtCode.size() * sizeof(BitCode); // 您的BitCode是bitset<128>
    std::cout << "GT数据总大小: " << gt_memory_footprint / (1024 * 1024) << " MB\n";

    // 循环k = 5 次 取均值 时间
    // k 改为命令行第 4 参 trial_count (默认 1), 方便脚本控制重复次数
    const int k = trial_count;
    std::vector<double> durations;
    exploreCR_call_count = 0;
    //这是总共的计时
    for (int iter = 0; iter < k; iter++) {  
        // 初始化全局变量
        no_edge_and_gt_get = 0;
        no_edge_but_gt_noget = 0;
        have_edge_and_gt_noget = 0;
        internal_edge_true_cnt = 0;
        internal_edge_false_cnt = 0;
        // no_superedge_and_gt_get = 0;
        // no_superedge_but_gt_noget = 0;
        // have_superedge_and_gt_noget = 0;
        current_match_cnt = 0;

        //计数器清零
        has_edge_in_data_total_calls = 0;
        dfs_total_calls = 0;
        explorecr_calls = 0;
        // 补清计时器 (原来只清计数器, 多 trial 时这些会跨轮累计, /k 平均口径就被破坏)
        explorecr_time_ms = 0.0;
            exploreCR_call_count = 0;
        dfs_total_time_all_ns = 0;

        auto start = std::chrono::steady_clock::now();
        // TURBOISO
        vector<unordered_map<int, int>> results = runMatch(Q);
        auto end = std::chrono::steady_clock::now();

        std::chrono::duration<double> duration = end - start;
        durations.push_back(duration.count());
        print_matches(results);// 输出结果 暂时不输出匹配
        // 结束一次实验
    }

    // 计算平均时间
    double avg_duration = 0.0;
    for (double d : durations) avg_duration += d;
    avg_duration /= k;
    std::cout << "has gt Average time over " << k << " iterations: " << avg_duration << " seconds\n";//一次完整实验的总时间

	// 在这里打印DFS步骤时间
	
	// ========== 性能统计 ==========
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

// q2 4节点钻石图
// 使用快速计数器 写死M3查边join
// 后置 ISJOIN过滤之后AN2
// 后置拦截AMN