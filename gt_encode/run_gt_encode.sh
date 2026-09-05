#!/bin/bash
# ============================================================
# run_gt_encode.sh (CGT)
# Build GT codes for the contracted-graph nodes: output_<ds>/gt_input_edges.txt -> GTBIN_contract/<ds>.bin
# Usage: gt_encoder <mode> <input> <output>, mode=2 build, mode=1 verify; KL=2
# Usage:
#   ./run_gt_encode.sh                (BK only by default)
#   DATASETS_OVERRIDE="BK LJ" ./run_gt_encode.sh
# ============================================================
cd "$(dirname "$0")" || exit 1

[ -n "$DATASETS_OVERRIDE" ] && DATASETS=($DATASETS_OVERRIDE) || DATASETS=( "BK"
    # "CH" "CTR" "DB" "EP" "GO" "HW" "IT" "LJ" "ND" "SP" "SD" "SF" "TW" "USA" "WT"
)

# ---- Build (via the root Makefile) ----
make -s -C .. gt_encode || { echo "build failed, abort"; exit 1; }

PROGRAM="./gt_encoder"
KL=2
# CGT data dirs (../../data/output_<ds>/ and ../../data/gtbin/)
BASE="$(cd "$(dirname "$0")/.." && pwd)"
INPUT_BASE_DIR="$BASE/data"
OUTPUT_BASE_DIR="$BASE/data/gtbin"
LOG_DIR="./logs_gt_contract"

mkdir -p "$LOG_DIR"
mkdir -p "$OUTPUT_BASE_DIR"

for dataset in "${DATASETS[@]}"; do
    LOG_FILE="${LOG_DIR}/${dataset}_$(date +%Y%m%d_%H%M%S).log"

    echo "========================================"
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Processing: $dataset"
    echo "Log: $LOG_FILE"
    echo "========================================"

    # uses gt_input_edges.txt (edge-list format of the contracted graph)
    INPUT_FILE="${INPUT_BASE_DIR}/output_${dataset}/gt_input_edges.txt"
    OUTPUT_FILE="${OUTPUT_BASE_DIR}/${dataset}.bin"

    if [ ! -f "$INPUT_FILE" ]; then
        echo "[ERROR] Input file not found: $INPUT_FILE" | tee -a "$LOG_FILE"
        echo "Available files:"
        ls -la "${INPUT_BASE_DIR}/output_${dataset}/" 2>&1 | tee -a "$LOG_FILE"
        continue
    fi

    # verify the file format
    FIRST_LINE=$(head -1 "$INPUT_FILE")
    echo "First line: $FIRST_LINE" | tee -a "$LOG_FILE"

    # run
    $PROGRAM $KL "$INPUT_FILE" "$OUTPUT_FILE" 2>&1 | tee "$LOG_FILE"

    if [ ${PIPESTATUS[0]} -eq 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] SUCCESS: $dataset" | tee -a "$LOG_FILE"
    else
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] FAILED: $dataset" | tee -a "$LOG_FILE"
    fi

    echo ""
done

echo "All tasks completed!"
