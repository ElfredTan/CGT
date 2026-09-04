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

// ========== 宏开关配置 (与 cda0729.cpp 对齐) ==========
// GT 剪枝开关: 1 = 使用 GT (0524染色版原行为), 0 = 不使用 GT
#ifndef USE_GT
#define USE_GT 1
#endif
// 查边统计开关: 1 = 记录 (原行为), 0 = 不记录 (性能优先)
#ifndef RECORD_EDGE_STATS
#define RECORD_EDGE_STATS 1
#endif

vector<string> dataNodeLabel;

// BK算法数据结构
// int st[500][20000];//如果是hollywood 可能需要修改这里
int st[3000][100000];

// 算法参数
int k;
// int ans;
int maxCliqueNumber = -1;  // -1 哨兵(无 clique), main 里循环会重新赋值

// 超节点相关
vector<int> superNodeCsMap;

// GT剪枝统计
#if RECORD_EDGE_STATS
long long no_edge_and_gt_get = 0;
long long no_edge_but_gt_noget = 0;
long long have_edge_and_gt_noget = 0;

#endif

// isNeighbor 内部边计数: nodeToSuper[u]==nodeToSuper[v] 时 +1 (只计数量, 不分 clique/star/path)
// 始终记录(不受 RECORD_EDGE_STATS 控制), 供脚本统计内部边占比。
long long internal_edge_checks = 0;

// 内部边查询细分: 查到边(判定有边) + 查到非边(判定无边) = 内部边查询总数
long long internal_edge_true_cnt = 0;
long long internal_edge_false_cnt = 0;


// 性能时间统计
double totalCheckTime = 0;

vector<int> globalDegreeHalf;

// ========== Coloring 上界剪枝 (greedy coloring bound, Tomita MCS 风格) ==========
// colorBound[] 复用静态数组, 避免每层反复分配 (候选集 sz 上限 = 100000)。
int colorBound[100000];
long long coloring_prune_count = 0;   // 统计: coloring 触发剪枝的次数
long long coloring_call_count = 0;    // 统计: coloring 被调用的次数
// 染色开关: !=0 全染色(所有 sz>1 候选集都着色, 不再有 sz 阈值门槛); =0 禁用(nocolor mode)。
// (历史: 曾用 sz 阈值 2000/64, 但 sz 无法区分健康/病态, 见 coloring_bound_experiment.md 七章, 故改为全染开关)
int g_coloring_min_sz = 1;

    // 计算所有节点的总度数（adjInternal.size() + adjExternal.size()）
void computeHalfDegree() {
    globalDegreeHalf.resize(numOriNodes);
    for (int i = 0; i < numOriNodes; i++) {
        globalDegreeHalf[i] = adjInternal[i].size() + adjExternal[i].size();
    }
}

// ========== CDA 核心算法函数 ==========
// 外部是一半邻接表的查边函数 isnei,cda会用,三角形不需要查内部边 不用
// 外部是全部邻接表的查边函数 hasedge，subac会用
bool isNeighbor(int u, int v) {
    // 内部边检测
    if (nodeToSuper[u] == nodeToSuper[v]) {
        internal_edge_checks++;   // 只计内部边数量, 不分类型
        int SUPERU = nodeToSuper[u];
        // 0809 single 守卫: single 仅 1 成员, 同超节点⇒u==v 自环, CDA 无自环→false
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
            // diamond {u,v,x,y}: member0(begin)和member1(end)与任意成员有边
            // 只有 member2-member3 之间无边; 即 a 或 b 是 begin/end 就有边
            int begin = superNodes[SUPERU].beginnode;
            int end = superNodes[SUPERU].endnode;
            if (u == begin || u == end || v == begin || v == end) {
                result = true;
            }
        }
        else { // path: CDA 候选集构造保证同一 path 的两节点不共存(只端点入候选, 见 hasKClique path 分支),
               // 故此分支正常下永不触发。0905 与 subac 统一: 有 path_pos 时用 |Δpos|==1 单层判定
               // (相邻⟺真边, 见 CGT/test/); 无则保持原 false (端点对间无直接边)。
            if (g_path_pos_ok) {
                result = queryPathPos((uint32_t)u, (uint32_t)v);
            } else {
                result = false;
            }
        }
        // 内部边细分统计 (直接计数, 不受 RECORD_EDGE_STATS 控制)
        if (result) internal_edge_true_cnt++;
        else        internal_edge_false_cnt++;
        return result;
    }

    // 外部路径
#if USE_GT
    // GT 剪枝路径: 先用 GT bitset 判断, 有公共邻居则无图边
    bool gt_has_common = (gtCode[u] & gtCode[v]).any();

    if (gt_has_common) {
#if RECORD_EDGE_STATS
        no_edge_and_gt_get++;
#endif
        //去掉GT异常检查
        return false;
    }
    // GT 判断无公共邻居, 继续邻接表查询
#endif

    // 因为是half
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

// greedy coloring: 对候选集 st[num][0..sz-1] 着色, 返回使用的颜色数 (上界)。
// 按 globalDegree 降序着色 (度数大的先着, 颜色数更紧)。复用静态 colorBound[]。
// 注意: 这里只读 st, 不修改它, 不影响 dfsK 的遍历顺序。
int greedyColoring(int sz, int num) {
    if (sz <= 0) return 0;
    // 收集候选集下标, 按度数降序排列 (用 colorBound 临时存 index 做排序buffer会污染,
    // 所以用一个简单选择: 由于 sz 可能很大, 这里用 sort on index array)
    // 为避免每层 new vector, 用静态 index buffer
    static int idxBuf[100000];
    for (int i = 0; i < sz; i++) idxBuf[i] = i;
    sort(idxBuf, idxBuf + sz, [num](int a, int b) {
        return globalDegree[st[num][a]] > globalDegree[st[num][b]];
    });

    // colorBound[i] 将存第 i 个 (排序后) 节点的颜色 (1-based, 0=未着色)
    // 复用全局 colorBound, 按 idxBuf 顺序着色
    int maxColor = 0;
    for (int oi = 0; oi < sz; oi++) {
        int i = idxBuf[oi];
        int u = st[num][i];
        // 标记已着色邻居用过的颜色。用版本号 trick 避免 O(sz) 清理 verTag。
        static int clearVer = 0;
        static int verTag[100000];
        clearVer++;
        int assigned = 0;
        for (int oj = 0; oj < oi; oj++) {
            int j = idxBuf[oj];
            // 仅当 j 已着色且与 u 相邻, 标记其颜色被占
            if (colorBound[j] != 0 && isNeighbor(u, st[num][j])) {
                verTag[colorBound[j]] = clearVer;  // 标记该颜色被占
            }
        }
        // 找最小未占用颜色
        int c = 1;
        while (c <= maxColor && verTag[c] == clearVer) c++;
        if (c > maxColor) maxColor = c;
        colorBound[i] = c;
    }
    return maxColor;
}

bool dfsK(int sz, int num, int k) {
    //不需要sz=0 随时检查和终止  sz=0而且还不够k的会提前终止或者会在后面有return false
    if (num >= k)
        return true;


    // ===== Coloring 上界剪枝 =====
    // 对当前候选集贪心着色, 得到颜色数 c (候选集内最大团的上界)。
    // 安全剪枝: num + c < k 则当前分支不可能凑出 k-clique, 直接返回。
    // 仅当阈值 > 0 且 sz 超过阈值时启用。COLORING_MIN_SZ=0 完全禁用 (退化为原版)。
    // 染色: g_coloring_min_sz != 0 则全染色(不看 sz 大小); =0 禁用(nocolor mode)
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

    // 从后往前 先找全局度数大的
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
        // 收集内部邻居 加载或者提前用摘要生成
        for (int v : adjInternal[i]) {
            if (globalDegree[v] >= k-1) {
                st[1][cnt++] = v;
            }
        }

        // 收集外部邻居
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
        //这里的排序st 而得到候选节点之间的相对顺序会被一直沿用到后面层的st

        bool found = dfsK(cnt, 1, k);

        if (found) {
            return true;
        }
    }
    return false;
}


//如果size相同 按照md降序
bool compareDescending_size_md_de(int a, int b) {
    if (superNodes[a].size != superNodes[b].size) {
        return superNodes[a].size > superNodes[b].size;
    }
    return superNodes[a].md > superNodes[b].md; // 这里虽然是次键 如果不用single的摘要 可以考虑分类讨论 或者直接分两种超节点排序 single和非single 反正这里先按照type主键排序；
}

bool hasKClique(int k) {
    vector<int> cliqueStart;

    // ===== 阶段1: clique 区 (id 0..maxCliqueNumber), 全非 single, 安全 =====
    for (int fakeC = 0; fakeC < maxCliqueNumber + 1; fakeC++) {
        int C = superNodeCsMap[fakeC];

        if (superNodes[C].md < k - 1) continue;
        cliqueStart.clear();
        cliqueStart = superNodeMembers[C];

        if (solvesubK(cliqueStart)) {
            return true;
        }
    }

    // ===== 阶段2: star/diamond/path 区, 上界用 min(firstSingleId, supernodenum) 切断 single 访问 =====
    int nonSingleEnd = min(firstSingleId, supernodenum);
    for (int C = maxCliqueNumber + 1; C < nonSingleEnd; C++) {
        if (superNodes[C].md < k - 1) continue;

        cliqueStart.clear();
        if(superNodes[C].type==4)//midnode.degree = 2  (path)
        {
            // 上游问题(非下游): ctr 实测(adj1验证) 当前 synopsis 的 begin=链尾邻居(度2)、
            // end=链尾, begin 不是链头。待收缩侧修正后此处自然恢复正确。
            // 下游按"摘要自包含"设计, 坚持读 synopsis, 不退回 members 推导。
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

    // ===== 阶段3: single 区 (firstSingleId..supernodenum-1), 无摘要, 用 globalDegree[成员] 替代 md =====
    for (int C = firstSingleId; C < supernodenum; C++) {
        int member = superNodeMembers[C][0];              // single 唯一成员
        if (globalDegree[member] < k - 1) continue;       // 用 globalDegree, 不是 md
        cliqueStart.clear();
        cliqueStart.push_back(member);
        if (solvesubK(cliqueStart)) {
            return true;
        }
    }
    return false;
}

void printCDAStats() {
    // cout << "===== DP Prune Statistics =====" << "\n";
    // }
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
    // 默认值
    kcandidates = {13, 15, 20, 21, 22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35};
    return kcandidates;
}


// ========== 主函数 ==========

int main(int argc, char** argv) {
    // 参数: prog <dataset_name> <trial_count> [k_values...]
    //   trial_count = 每个 k 重复实验次数 (默认 1); k_values 省略则用数据集默认 k 列表
    if (argc < 2) {
        cout << "Usage: " << argv[0] << " <dataset_name> [trial_count] [k_values...]" << endl;
        cout << "Example: " << argv[0] << " youtube 5 16 17 18 19 20" << endl;
        return 1;
    }

    // 获取数据集名称
    string dataset_name = argv[1];

    // 第2参: trial_count (可选, 默认 1)。判断 argv[2] 是否为纯数字
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

    // 读 coloring 阈值 (环境变量覆盖, 默认 256)
    if (const char* env = getenv("COLORING_MIN_SZ")) {
        g_coloring_min_sz = atoi(env);
    }
    cout << "coloring_min_sz = " << g_coloring_min_sz
         << " (set COLORING_MIN_SZ env to override; 0=disable coloring)" << endl;

    cout << "Version: cda0524_coloring trial_count=" << trial_count << endl;

    // 构建基于数据集名称的路径前缀
    string output_prefix = "../data/output_" + dataset_name + "/";
    string gtbin_path = "../data/gtbin/" + dataset_name + ".bin";

    cout << "Dataset: " << dataset_name << endl;

    // 通过数据集参数加载
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
    loadMembersTxt((output_prefix + "members.txt").c_str());   // fC' 填 superNodeMembers(保序, 含single)
    cout << "version 20260524 gt edge" << endl;

    globalDegree.clear();
    globalDegree.resize(numOriNodes);

    adjInternal.resize(numOriNodes);
    loadInternalAdjHalf((output_prefix + "internal_adj.txt").c_str());

    loadExternalAdjHalf((output_prefix + "external_adj.txt").c_str());

    // path 内部 O(1) 查边结构 (缺失则 g_path_pos_ok=false, isNeighbor path 分支保持 false)
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
    // 计算所有节点的总度数（adjInternal.size() + adjExternal.size()）
    computeHalfDegree();


    // 确定 k 值列表
    vector<int> kcandidates;
    if (k_arg_start < argc) {
        for (int i = k_arg_start; i < argc; i++) {
            kcandidates.push_back(stoi(argv[i]));
        }
    } else {
        // 默认 k 值（根据不同数据集）
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
            totalCheckTime = 0;

            auto start = std::chrono::steady_clock::now();

            superNodeCsMap.resize(supernodenum);
            maxCliqueNumber = -1;  // -1 哨兵: 无 clique 区 (全 single 或 supernodenum==0)
            for (int idx = 0; idx < supernodenum; idx++) {
                superNodeCsMap[idx] = idx;
                if (superNodes[idx].type != 1) {  // !=clique; single 区 type=0(空) 必触发 break
                    maxCliqueNumber = idx - 1;
                    break;
                }
                maxCliqueNumber = idx;  // 走到末尾仍全 clique
            }

            int maxcliquesize = 0;
            if (maxCliqueNumber >= 0) {
                sort(superNodeCsMap.begin(), superNodeCsMap.begin() + maxCliqueNumber + 1, compareDescending_size_md_de);
                maxcliquesize = superNodes[superNodeCsMap[0]].size;  // [0] 必是 clique, 安全
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
