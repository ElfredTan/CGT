#!/bin/bash
# ============================================================
# run_cdac.sh — kclique (cdac) 染色版实验脚本
# 两个正交维度: 染色 (coloring/nocolor) × GT (gt/gtwithcount)
#
# 用法: ./run_coloring.sh [color_mode] [gt_mode] [DATASETS...]
#   color_mode  coloring(默认) | nocolor
#               coloring = COLORING_MIN_SZ=1 (全染色; hollywood 会退化, 已知)
#               nocolor  = COLORING_MIN_SZ=0  (禁用染色)
#   gt_mode     gtwithcount(默认) | gt   (0905 两模式统一, 旧名兼容映射)
#               gtwithcount = USE_GT=1 RECORD_EDGE_STATS=1 (开GT+记录查边, 旧名 gt)
#               gt          = USE_GT=1 RECORD_EDGE_STATS=0 (开GT不记录查边, 旧名 noedge)
#   DATASETS    要跑的数据集(默认用下方 DEFAULT_DATASETS)
#
# 示例:
#   ./run_coloring.sh                       # coloring + gt + DEFAULT_DATASETS
#   ./run_coloring.sh nocolor gt hollywood  # 无染色 + 开GT
# ============================================================

set -u

# ==================== 参数解析 ====================
# 参数: [color_mode] [gt_mode] [DATASETS...]
#   color_mode: coloring(默认) | nocolor
#   gt_mode:    gtwithcount(默认) | gt
COLOR_MODE="${1:-coloring}"
case "$COLOR_MODE" in
    coloring|COLORING) COLORING_DEFAULT_SZ=1 ;;   # 1=全染色(sz>1都染)
    nocolor|NOCOLOR)   COLORING_DEFAULT_SZ=0  ;;
    *) DATASETS="$*"; COLOR_MODE="coloring"; COLORING_DEFAULT_SZ=1 ;;  # 第一参是数据集
esac
# 只有当第一参是 color_mode 时才消费它
if [ "$COLOR_MODE" = "coloring" -o "$COLOR_MODE" = "nocolor" ]; then
    shift 2>/dev/null || true
    GT_MODE="${1:-gt}"
    case "$GT_MODE" in
        gtwithcount)   USE_GT=1; RECORD_EDGE_STATS=1 ;;
        gt)            USE_GT=1; RECORD_EDGE_STATS=0 ;;
        # 兼容旧名
        noedge|NOEDGE) USE_GT=1; RECORD_EDGE_STATS=0; GT_MODE="gt" ;;
        *) USE_GT=1; RECORD_EDGE_STATS=1; GT_MODE="gtwithcount" ;;  # 第二参不是 gt_mode, 当数据集
    esac
    # 只有当第二参是 gt_mode 时才消费它
    case "$GT_MODE" in
        gtwithcount|gt|noedge) shift 2>/dev/null || true ;;
    esac
    DATASETS="${*:-}"
fi
GT_MODE="${GT_MODE:-gt}"; USE_GT="${USE_GT:-1}"; RECORD_EDGE_STATS="${RECORD_EDGE_STATS:-1}"

# ==================== 配置区(改这里) ====================

# 单次查询超时秒数 (一次 k-clique 查询的预算; 总超时 = 此值 × k个数 × trial数)
TIMEOUT_PER_QUERY=300

# 每个 mode 的重复实验次数 (传给 cpp 作 trial_count)
declare -A TRIALS
TRIALS[coloring]=5
TRIALS[nocolor]=5

# 默认要跑的数据集(裸跑时不传数据集参数就用这个)。
# 跑之前在这里注释/反注释来选要跑哪些(数组写法, # 是真注释)。
DEFAULT_DATASETS=(
    "bk"
    # "citehepth"
)
# 每个数据集要测的 k 值(空格分隔; 与 cda0524.cpp 的 kcandi_init 一致)
declare -A K_VALUES
K_VALUES[bk]="197 199 201 202 204 206 208"
K_VALUES[citehepth]="21 22 23 24 25 26 27"
K_VALUES[ctr]="4 5 6 7 8 9 10"
K_VALUES[dblp]="114 115 116 117 118 119 120"
K_VALUES[ep]="21 22 23 24 25 26 27"
K_VALUES[google]="42 43 44 45 46 47 48"
K_VALUES[hollywood]="2199 2204 2209 2210 2215 2220 2225"
K_VALUES[lj]="323 325 327 328 330 332 334"
K_VALUES[notredame]="153 154 155 156 157 158 159"
K_VALUES[pokec]="27 28 29 30 31 32 33"
K_VALUES[slash]="25 26 27 28 29 30 31"
K_VALUES[stanford]="59 60 61 62 63 64 65"
K_VALUES[twitter]="69 70 71 72 73 74 75"
K_VALUES[usa]="4 5 6 7 8 9 10"
K_VALUES[wiki]="24 25 26 27 28 29 30"
# it


# 可选: 单独覆盖某数据集的 COLORING_MIN_SZ (留空则用 mode 默认值 64 或 0) 统一对某数据集cgt/gcon/gt/ori进行染色或者不染色
declare -A COLOR_SZ

[ -z "$DATASETS" ] && DATASETS="${DEFAULT_DATASETS[*]}"

# ==================== 以下一般不用改 ====================

SRCDIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SRCDIR" || exit 1

EXE="cdac"
# 按 color_mode/gt_mode 分子目录: logs_coloring/<color_mode>_<gt_mode>/
LOGDIR="logs_coloring/${COLOR_MODE}_${GT_MODE}"
mkdir -p "$LOGDIR"

echo "===== 编译 $EXE (USE_GT=$USE_GT RECORD_EDGE_STATS=$RECORD_EDGE_STATS) ====="
if ! make -s -C .. cdac MODE="$GT_MODE"; then
    echo "编译失败, 终止"; exit 1
fi
echo "编译完成: $EXE (color_mode=$COLOR_MODE COLORING_MIN_SZ=$COLORING_DEFAULT_SZ, gt_mode=$GT_MODE)"
echo ""

# ===== 跑实验 + 存 log =====
THIS_RUN_LOGS=""
TRIAL="${TRIALS[$COLOR_MODE]}"
for ds in $DATASETS; do
    ks="${K_VALUES[$ds]:-}"
    LOG="$LOGDIR/${ds}.log"
    # 本数据集的 COLORING_MIN_SZ: COLOR_SZ 覆盖 > mode 默认
    if [ -n "${COLOR_SZ[$ds]:-}" ]; then
        csz="${COLOR_SZ[$ds]}"
    else
        csz="$COLORING_DEFAULT_SZ"
    fi
    if [ -n "$ks" ]; then
        nk=$(echo $ks | wc -w)
    else
        nk=1
    fi
    TOTAL_TIMEOUT=$(( TIMEOUT_PER_QUERY * nk * TRIAL ))
    echo "----- 跑 $ds  k=[$ks]  trial=$TRIAL  COLORING_MIN_SZ=$csz  总超时=${TOTAL_TIMEOUT}s → $LOG -----"
    if [ -n "$ks" ]; then
        COLORING_MIN_SZ=$csz timeout "$TOTAL_TIMEOUT" ./"$EXE" "$ds" "$TRIAL" $ks > "$LOG" 2>&1
    else
        COLORING_MIN_SZ=$csz timeout "$TOTAL_TIMEOUT" ./"$EXE" "$ds" "$TRIAL" > "$LOG" 2>&1
    fi
    rc=$?
    if [ $rc -eq 124 ]; then
        echo "  ⚠ $ds 超时(>${TOTAL_TIMEOUT}s), log 可能不完整"
    fi
    THIS_RUN_LOGS="$THIS_RUN_LOGS $LOG"
done
echo ""

# ===== awk 提取本次 log 为 tsv (多 coloring_calls / coloring_prune 两列) =====
TSV="$LOGDIR/zsummary_$(date +%Y%m%d_%H%M%S).tsv"

awk -v cmode="$COLOR_MODE" -v gmode="$GT_MODE" '
    function flush() {
        if (cur_k != "") {
            printf "%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\t%s\n", \
                cmode, gmode, cur_ds, cur_k, cur_w, cur_c, cur_dec, cur_t, \
                stat1, stat2, stat3, ccall, cprune, iedge, ietru, iefalse
        }
        cur_k=""; cur_w=""; cur_c=""; cur_dec=""; cur_t=""
        stat1="NA"; stat2="NA"; stat3="NA"; ccall="NA"; cprune="NA"; iedge="NA"; ietru="NA"; iefalse="NA"
    }
    BEGIN {
        print "color_mode\tgt_mode\tdataset\tk\tmax_contracted_clique_size\tmax_contracted_clique_C\tdecision\ttime_s\tno_edge_and_gt_get\tno_edge_but_gt_noget\thave_edge_and_gt_noget\tcoloring_calls\tcoloring_prune\tinternal_edge_checks\tinternal_edge_true\tinternal_edge_false"
        stat1="NA"; stat2="NA"; stat3="NA"; ccall="NA"; cprune="NA"; iedge="NA"; ietru="NA"; iefalse="NA"
    }
    /^Dataset: / { flush(); cur_ds=$2 }
    /--------check k clique when k = / {
        flush()
        n=split($0, a, "="); gsub(/[^0-9]/, "", a[2]); cur_k=a[2]
    }
    /max contracted clique size = / { n=split($0,a,"="); gsub(/[^0-9]/,"",a[2]); cur_w=a[2] }
    /max contracted clique C = /    { n=split($0,a,"="); gsub(/[^0-9]/,"",a[2]); cur_c=a[2] }
    /yes for the/         { cur_dec="yes_instant" }
    /^yes [0-9]+ clique/  { cur_dec="yes" }
    /^no [0-9]+ clique/   { cur_dec="no" }
    /Average time taken:/ { cur_t=$4 }
    /===== check edge stats =====/ { getline s1; getline s2; getline s3; stat1=s1; stat2=s2; stat3=s3 }
    /coloring_calls:/ { gsub(/[^0-9]/,"",$2); ccall=$2 }
    /coloring_prune:/ { gsub(/[^0-9]/,"",$2); cprune=$2 }
    /internal_edge_checks:/ { gsub(/[^0-9]/,"",$2); iedge=$2 }
    /internal_edge_true:/  { gsub(/[^0-9]/,"",$2); ietru=$2 }
    /internal_edge_false:/ { gsub(/[^0-9]/,"",$2); iefalse=$2 }
    END { flush() }
' $THIS_RUN_LOGS > "$TSV"

echo "===== tsv 已生成: $TSV ====="
echo ""
cat "$TSV"
echo ""
echo "完成。log 在 $LOGDIR/, tsv 在 $TSV"
