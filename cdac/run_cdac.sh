#!/bin/bash
# ============================================================
# run_cdac.sh - k-clique (cdac) coloring experiment script
# Two orthogonal dimensions: coloring (coloring/nocolor) x GT (gt/gtwithcount)
#
# Usage: ./run_cdac.sh [color_mode] [gt_mode] [DATASETS...]
#   color_mode  coloring (default) | nocolor
#               coloring = COLORING_MIN_SZ=1 (color everything; known to regress on HW)
#               nocolor  = COLORING_MIN_SZ=0  (coloring disabled)
#   gt_mode     gt (default) | gtwithcount   (two-mode convention)
#               gtwithcount = USE_GT=1 RECORD_EDGE_STATS=1 (GT on + counters recorded)
#               gt          = USE_GT=1 RECORD_EDGE_STATS=0
#   DATASETS    datasets to run (defaults to DEFAULT_DATASETS below)
#
# Examples:
#   ./run_cdac.sh                       # coloring + gt + DEFAULT_DATASETS
#   ./run_cdac.sh nocolor gt HW         # no coloring + GT on
# ============================================================

set -u

# ==================== Argument parsing ====================
# Args: [color_mode] [gt_mode] [trials] [DATASETS...]
#   color_mode: coloring (default) | nocolor
#   gt_mode:    gt (default) | gtwithcount
#   trials:     repetitions per k, default 5
COLOR_MODE="${1:-coloring}"
case "$COLOR_MODE" in
    coloring|COLORING) COLORING_DEFAULT_SZ=1 ;;   # 1 = color everything (sz>1)
    nocolor|NOCOLOR)   COLORING_DEFAULT_SZ=0  ;;
    *) DATASETS="$*"; COLOR_MODE="coloring"; COLORING_DEFAULT_SZ=1 ;;  # first arg is a dataset
esac
# consume the first arg only when it is a color_mode
if [ "$COLOR_MODE" = "coloring" -o "$COLOR_MODE" = "nocolor" ]; then
    shift 2>/dev/null || true
    GT_MODE="${1:-gt}"
    case "$GT_MODE" in
        gtwithcount)   USE_GT=1; RECORD_EDGE_STATS=1 ;;
        gt)            USE_GT=1; RECORD_EDGE_STATS=0 ;;
        # not a gt_mode: treat the arg as a dataset, fall back to the default gt
        *) USE_GT=1; RECORD_EDGE_STATS=0; GT_MODE="gt" ;;
    esac
# consume the second arg only when it is a gt_mode
    case "$GT_MODE" in
        gtwithcount|gt) shift 2>/dev/null || true ;;
    esac
    # consume the third arg only when it is numeric (trials)
    if [ $# -gt 0 ] && [[ "$1" =~ ^[0-9]+$ ]]; then
        TRIALS="$1"
        shift 2>/dev/null || true
    fi
    DATASETS="${*:-}"
fi
GT_MODE="${GT_MODE:-gt}"; USE_GT="${USE_GT:-1}"
# default RECORD_EDGE_STATS follows GT_MODE (gt -> 0, gtwithcount -> 1); explicit env wins
if [ -z "${RECORD_EDGE_STATS:-}" ]; then
    [ "$GT_MODE" = "gtwithcount" ] && RECORD_EDGE_STATS=1 || RECORD_EDGE_STATS=0
fi
# repetitions per k, default 5 (set by the 3rd arg above)
TRIALS="${TRIALS:-5}"

# ==================== Configuration (edit here) ====================

# Per-query timeout seconds (budget of one k-clique query; total = this x #k x #trials)
TIMEOUT_PER_QUERY=300

# Repetitions per k (TRIALS, default 5) are passed to the binary as trial_count.

# Default datasets (used when no dataset args are given).
# Comment/uncomment entries here to select what runs (array syntax; # is a real comment).
DEFAULT_DATASETS=(
    "BK"
    # "CH"
)
# k values per dataset (space separated; matches kcandi_init of the original cda0524.cpp)
# A few steps around the ORIGINAL graph's maximum clique ω (center = ω, ω+1); the step grows with the clique size.
# Extending leftward, adjust automatically at the max contracted clique size (usually smaller than ω).
declare -A K_VALUES
K_VALUES[BK]="197 199 201 202 204 206 208"
K_VALUES[CH]="21 22 23 24 25 26 27"
K_VALUES[CTR]="4 5 6 7 8 9 10"
K_VALUES[DB]="114 115 116 117 118 119 120"
K_VALUES[EP]="21 22 23 24 25 26 27"
K_VALUES[GO]="42 43 44 45 46 47 48"
K_VALUES[HW]="2199 2204 2209 2210 2215 2220 2225"
K_VALUES[IT]="3202 3212 3222 3223 3233 3243 3253" # IT csr
K_VALUES[LJ]="323 325 327 328 330 332 334"
K_VALUES[ND]="153 154 155 156 157 158 159"
K_VALUES[SP]="27 28 29 30 31 32 33"
K_VALUES[SD]="25 26 27 28 29 30 31"
K_VALUES[SF]="59 60 61 62 63 64 65"
K_VALUES[TW]="69 70 71 72 73 74 75"
K_VALUES[USA]="4 5 6 7 8 9 10"
K_VALUES[WT]="24 25 26 27 28 29 30"



# Optional: override COLORING_MIN_SZ per dataset (empty = mode default 64 or 0), to color or not per dataset
declare -A COLOR_SZ
# it nocoloring 
[ -z "$DATASETS" ] && DATASETS="${DEFAULT_DATASETS[*]}"

# ==================== Nothing below normally needs changes ====================

SRCDIR="$(cd "$(dirname "$0")" && pwd)"
cd "$SRCDIR" || exit 1

EXE="cdac"
# Subdirs per color_mode/gt_mode: logs_coloring/<color_mode>_<gt_mode>/
LOGDIR="logs_coloring/${COLOR_MODE}_${GT_MODE}"
mkdir -p "$LOGDIR"

echo "===== building $EXE (USE_GT=$USE_GT RECORD_EDGE_STATS=$RECORD_EDGE_STATS) ====="
rm -f ./cdac   # MODE is a compile-time macro: force rebuild so the binary always matches $GT_MODE
if ! make -s -C .. cdac MODE="$GT_MODE"; then
    echo "build failed, abort"; exit 1
fi
echo "build done: $EXE (color_mode=$COLOR_MODE COLORING_MIN_SZ=$COLORING_DEFAULT_SZ, gt_mode=$GT_MODE)"
echo ""

# ===== Run experiments + save logs =====
THIS_RUN_LOGS=""
TRIAL="$TRIALS"
for ds in $DATASETS; do
    ks="${K_VALUES[$ds]:-}"
    LOG="$LOGDIR/${ds}.log"
    # COLORING_MIN_SZ for this dataset: COLOR_SZ override > mode default
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
    echo "----- run $ds  k=[$ks]  trial=$TRIAL  COLORING_MIN_SZ=$csz  total timeout=${TOTAL_TIMEOUT}s -> $LOG -----"
    if [ -n "$ks" ]; then
        COLORING_MIN_SZ=$csz timeout "$TOTAL_TIMEOUT" ./"$EXE" "$ds" "$TRIAL" $ks > "$LOG" 2>&1
    else
        COLORING_MIN_SZ=$csz timeout "$TOTAL_TIMEOUT" ./"$EXE" "$ds" "$TRIAL" > "$LOG" 2>&1
    fi
    rc=$?
    if [ $rc -eq 124 ]; then
        echo "  WARN: $ds timed out (>${TOTAL_TIMEOUT}s), log may be incomplete"
    fi
    THIS_RUN_LOGS="$THIS_RUN_LOGS $LOG"
done
echo ""

# ===== awk extracts the logs into a tsv (incl. coloring_calls / coloring_prune columns) =====
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

echo "===== tsv written: $TSV ====="
echo ""
cat "$TSV"
echo ""
echo "done. logs in $LOGDIR/, tsv in $TSV"
