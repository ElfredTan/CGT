#!/bin/bash
# ============================================================
# run_subac.sh (CGT) - subgraph matching batch script
# Source: subac.cpp (six-in-one query dispatcher, q1..q6 with fixed NEC order, contiguous numbering)
# Graph layer: common/ (built via the root Makefile, all -O3)
# Data: ../data/output_<ds>/ and ../data/gtbin/<ds>.bin
#
# MODE, two variants (repo-wide convention):
#   gt           GT on, no edge-check counters
#   gtwithcount  GT on, record edge-check counters
#
# Usage:
#   ./run_subac.sh [trials]                       # trials: repetitions per query (default 5)
#   MODE=gtwithcount ./run_subac.sh
#   QUERIES_OVERRIDE="6" DATASETS_OVERRIDE="BK" ./run_subac.sh 3
# ============================================================
cd "$(dirname "$0")" || exit 1

MODE="${MODE:-gt}"
case "$MODE" in
    gt|gtwithcount) ;;
    *) echo "Unknown MODE: $MODE (use: gt | gtwithcount)"; exit 1 ;;
esac

[ -n "$QUERIES_OVERRIDE" ] && QUERIES=($QUERIES_OVERRIDE) || QUERIES=(1 2 3 4 5 6)
# 1st arg = repetitions per query (default 5)
TRIALS="${1:-5}"
MATCHTARGET="${MATCHTARGET:-100000000}"
[ -n "$DATASETS_OVERRIDE" ] && DATASETS=($DATASETS_OVERRIDE) || DATASETS=(
    "BK"
)

# ---- Build (via the root Makefile) ----
echo "[$(date '+%H:%M:%S')] building subac (MODE=$MODE) ..."
rm -f ./subac   # MODE is a compile-time macro: force rebuild so the binary always matches $MODE
make -s -C .. subac MODE=$MODE || { echo "build failed, abort"; exit 1; }
EXE="./subac"

SUMMARY_DIR="./logs_subac_${MODE}"
SUMMARY_FILE="${SUMMARY_DIR}/summary_$(date '+%Y%m%d_%H%M%S').tsv"
mkdir -p "$SUMMARY_DIR"

# TSV header (gtwithcount has the GT counter section; gt does not)
if [ "$MODE" = "gtwithcount" ]; then
    echo $'query\tdataset\ttotal_time\tembeddings\tstatus\thas_edge_calls\tinternal\tint_edge\tint_nonedge\tGT_hit\tGT_miss_nonedge\tGT_miss_edge\tGT_hit_rate\tdfs_time\tdfs_calls\tcr_time\tcr_calls' > "$SUMMARY_FILE"
else
    echo $'query\tdataset\ttotal_time\tembeddings\tstatus\tdfs_time\tdfs_calls\tcr_time\tcr_calls' > "$SUMMARY_FILE"
fi

echo "=========================================="
echo " CGT subac Batch - MODE=$MODE TRIALS=$TRIALS"
echo " Summary: $SUMMARY_FILE"
echo "=========================================="

for Q in "${QUERIES[@]}"; do
    LOG_DIR="./logs_subac_q${Q}_${MODE}"
    mkdir -p "$LOG_DIR"

    for DATASET in "${DATASETS[@]}"; do
        LOG_FILE="${LOG_DIR}/subac_q${Q}_${DATASET}_$(date '+%Y%m%d_%H%M%S').log"
        echo "[$(date '+%H:%M:%S')] q${Q} ${DATASET} ..."
        timeout 320s $EXE $MATCHTARGET $DATASET $Q $TRIALS > "$LOG_FILE" 2>&1
        EXIT_CODE=$?

        TOTAL_TIME=$(grep -oP 'has gt Average time over \d+ iterations: \K[\d.]+' "$LOG_FILE" | head -1)
        EMBEDDINGS=$(grep -oP 'Found \K\d+' "$LOG_FILE" | head -1)
        [ -z "$EMBEDDINGS" ] && EMBEDDINGS=$(grep -oP 'current_match_cnt: \K\d+' "$LOG_FILE" | head -1)

        DFS_TIME=$(grep -oP 'Total DFS time \(all depths\): \K[\d.]+' "$LOG_FILE" | head -1)
        DFS_CALLS=$(grep -oP 'Total DFS calls: \K\d+' "$LOG_FILE" | head -1)
        CR_TIME=$(grep -oP 'CR build time: \K[\d.]+' "$LOG_FILE" | head -1)
        CR_CALLS=$(grep -oP 'CR recursive calls: \K\d+' "$LOG_FILE" | head -1)

        if grep -qE 'ALARM TIMEOUT|\[TIMEOUT\]' "$LOG_FILE"; then
            STATUS="Timeout"
        elif [ "$EXIT_CODE" -eq 124 ]; then
            STATUS="HardKill"
            TOTAL_TIME="TIMEOUT"
        else
            STATUS="Complete"
        fi
        [ "$EXIT_CODE" -ne 0 ] && [ "$EXIT_CODE" -ne 124 ] && STATUS="Crash(rc=$EXIT_CODE)"

        if [ "$MODE" = "gtwithcount" ]; then
            HEC=$(grep "has_edge_in_data calls:" "$LOG_FILE" | grep -oP '\d+' | head -1)
            INT_EDGE=$(grep "int_edge:" "$LOG_FILE" | grep -oP '\d+' | head -1)
            INT_NONEDGE=$(grep "int_nonedge:" "$LOG_FILE" | grep -oP '\d+' | head -1)
            GT_HIT=$(grep "GT hit (skip bs):" "$LOG_FILE" | grep -oP '\d+' | head -1)
            GT_MISSNE=$(grep "GT miss (non-edge):" "$LOG_FILE" | grep -oP '\d+' | head -1)
            GT_MISSE=$(grep "edge (GT miss):" "$LOG_FILE" | grep -oP '\d+' | head -1)
            GT_RATE=$(grep "GT hit rate:" "$LOG_FILE" | grep -oP '[\d.]+' | head -1)
            echo "subac_q${Q}"$'\t'"${DATASET}"$'\t'"${TOTAL_TIME:-NA}"$'\t'"${EMBEDDINGS:-NA}"$'\t'"${STATUS}"$'\t'"${HEC:-0}"$'\t'"$((INT_EDGE + INT_NONEDGE))"$'\t'"${INT_EDGE:-0}"$'\t'"${INT_NONEDGE:-0}"$'\t'"${GT_HIT:-0}"$'\t'"${GT_MISSNE:-0}"$'\t'"${GT_MISSE:-0}"$'\t'"${GT_RATE:-0}"$'\t'"${DFS_TIME:-NA}"$'\t'"${DFS_CALLS:-0}"$'\t'"${CR_TIME:-NA}"$'\t'"${CR_CALLS:-0}" >> "$SUMMARY_FILE"
        else
            echo "subac_q${Q}"$'\t'"${DATASET}"$'\t'"${TOTAL_TIME:-NA}"$'\t'"${EMBEDDINGS:-NA}"$'\t'"${STATUS}"$'\t'"${DFS_TIME:-NA}"$'\t'"${DFS_CALLS:-0}"$'\t'"${CR_TIME:-NA}"$'\t'"${CR_CALLS:-0}" >> "$SUMMARY_FILE"
        fi
        echo "[$(date '+%H:%M:%S')] q${Q} ${DATASET}: ${TOTAL_TIME:-?}s, ${EMBEDDINGS:-?}emb, ${STATUS}"
    done
done

echo "[$(date '+%H:%M:%S')] all done. summary: $SUMMARY_FILE"
