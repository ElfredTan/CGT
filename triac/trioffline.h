#!/bin/bash
# ============================================================
# run_tricount.sh (CGT/triac) - triangle counting runner
#
# Division of work:
#   - non-3-outside triangles (internal/one-in-two-out/two-in-one-out): computed offline by the contraction pipeline
#     (trioffline), saved to ../data/output_<ds>/triangles_non3out.txt
#   - this script: runs triac_gt for 3-outside triangles and reads triangles_non3out.txt
#     to report total = non-3-outside + 3-outside; the offline count is not rerun
#
# Usage:
#   ./run_tricount.sh
#   DATASETS_OVERRIDE="BK LJ" ./run_tricount.sh [trials]      # trials: repetitions, default 5
#   OFFLINE=1 ./run_tricount.sh       # additionally run trioffline standalone to verify non-3-outside
# ============================================================
cd "$(dirname "$0")" || exit 1

# 1st arg = repetitions for the timing loop of triac_gt (default 5)
TRIALS="${1:-5}"

[ -n "$DATASETS_OVERRIDE" ] && DATASETS=($DATASETS_OVERRIDE) || DATASETS=(
    "BK"
)

# ---- Build (via the root Makefile) ----
make -s -C .. triac_gt || { echo "build failed, abort"; exit 1; }
[ "${OFFLINE:-0}" = "1" ] && make -s -C .. trioffline || true

LOG_DIR="./logs_tricount"
mkdir -p "$LOG_DIR"

for dataset in "${DATASETS[@]}"; do
    echo "======== $dataset ========"
    LOG_FILE="${LOG_DIR}/gt_${dataset}.log"

    # ---- 3-outside triangles (triac_gt) ----
    ./triac_gt "$dataset" "$TRIALS" > "$LOG_FILE" 2>&1
    tri3out=$(grep -oP 'triangleCount:\s*\K[0-9]+' "$LOG_FILE")
    echo "  3-outside (triac_gt):   $tri3out"

    # ---- non-3-outside (saved by the contraction side job) ----
    NON3_FILE="../data/output_${dataset}/triangles_non3out.txt"
    non3out=""
    if [ -f "$NON3_FILE" ]; then
        non3out=$(grep -oP '^total_non3out=\K[0-9]+' "$NON3_FILE")
    fi
    if [ -n "$non3out" ]; then
        echo "  non-3-outside (gcon):   $non3out"
        echo "  total triangles (sum):  $((non3out + tri3out))"
    else
        echo "  [WARN] $NON3_FILE not found - run gcon/run_gcon.sh first to produce the non-3-outside count"
        [ "${OFFLINE:-0}" = "1" ] && echo "  (OFFLINE=1 runs trioffline standalone for verification)"
    fi
    echo ""
done

# ---- Optional: run trioffline standalone to verify non-3-outside ----
if [ "${OFFLINE:-0}" = "1" ]; then
    for dataset in "${DATASETS[@]}"; do
        LOG_FILE="${LOG_DIR}/offline_${dataset}.log"
        ./trioffline "$dataset" > "$LOG_FILE" 2>&1
        echo "offline check $dataset: $(grep -oP 'total: \K[0-9]+' "$LOG_FILE") (non-3-outside)"
    done
fi
