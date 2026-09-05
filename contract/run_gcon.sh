#!/bin/bash
# ============================================================
# run_gcon.sh (CGT)
# Batch contraction: raw graph -> ../data/output_<dataset>/

#   (path member-order fix; main writes ../data/output_<ds>/, reads ../data/raw/test_data/<ds>.txt)
# Usage:
#   ./run_gcon.sh                (BK only by default)
#   DATASETS_OVERRIDE="BK LJ" ./run_gcon.sh
# ============================================================
cd "$(dirname "$0")" || exit 1

# ===== Configurable parameters =====
# KU: contraction upper bound; SHAPES: shape order (note: main.cpp currently ignores argv[3]; kept for the interface)
KU=500
SHAPES=""

[ -n "$DATASETS_OVERRIDE" ] && DATASETS=($DATASETS_OVERRIDE) || DATASETS=(
    "BK"
    # ""
)

# ---- Build (via the root Makefile) ----
echo "[$(date '+%H:%M:%S')] building contract (make contract) ..."
make -s -C .. contract || { echo "build failed, abort"; exit 1; }
PROGRAM="./gcon"

LOG_DIR="./logs_contract"
mkdir -p "$LOG_DIR"

echo "========================================"
echo "Starting batch contracting..."
echo "Program: $PROGRAM (KU=$KU, SHAPES=${SHAPES:-<default>})"
echo "Total datasets: ${#DATASETS[@]}"
echo "Log dir: $LOG_DIR"
echo "========================================"

SUCCESS=()
FAILED=()

for dataset in "${DATASETS[@]}"; do
    LOG_FILE="${LOG_DIR}/${dataset}_$(date +%Y%m%d_%H%M%S).log"

    echo "[$(date '+%Y-%m-%d %H:%M:%S')] Processing: $dataset"
    echo "Log: $LOG_FILE"

    if [ -z "$SHAPES" ]; then
        $PROGRAM "$dataset" "$KU" > "$LOG_FILE" 2>&1
    else
        $PROGRAM "$dataset" "$KU" "$SHAPES" > "$LOG_FILE" 2>&1
    fi
    EXIT_CODE=$?

    if [ $EXIT_CODE -ne 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] FAILED: $dataset (exit=$EXIT_CODE, check log: $LOG_FILE)"
        FAILED+=("$dataset")
        continue
    fi
    echo "[$(date '+%Y-%m-%d %H:%M:%S')] CONTRACTED: $dataset -> ../output_${dataset}/"

    SUCCESS+=("$dataset")
done

echo "========================================"
echo "Summary"
echo "========================================"
echo "Contracted OK:    ${#SUCCESS[@]} / ${#DATASETS[@]}"
echo "Contract FAILED:  ${FAILED[@]:-none}"
echo "Logs: $LOG_DIR"

# ===== Contraction side job - non-3-outside triangle counting (trioffline) =====
# Results go to ../data/output_<ds>/triangles_non3out.txt, read back by triac/run_tricount.sh
# trioffline is the nogt variant: it does not read gtbin, so no ordering dependency on gt_encode.
make -s -C .. trioffline || { echo "trioffline build failed, abort"; exit 1; }
for dataset in "${SUCCESS[@]}"; do
    echo "[$(date '+%H:%M:%S')] non-3-outside triangle count: $dataset ..."
    ( cd ../triac && ./trioffline "$dataset" > /tmp/tri_non3out_${dataset}.log 2>&1 )
    if [ $? -ne 0 ]; then
        echo "  [WARN] trioffline failed for $dataset (see /tmp/tri_non3out_${dataset}.log); not writing triangles_non3out.txt"
        continue
    fi
    internal=$(grep -oP 'internal triangles \(internal\): \K[0-9]+' /tmp/tri_non3out_${dataset}.log)
    oneIn=$(grep -oP 'oneInTwoOut\): \K[0-9]+' /tmp/tri_non3out_${dataset}.log)
    twoIn=$(grep -oP 'twoInOneOut\): \K[0-9]+' /tmp/tri_non3out_${dataset}.log)
    total=$(grep -oP 'total: \K[0-9]+' /tmp/tri_non3out_${dataset}.log)
    OUT_DIR="../data/output_${dataset}"
    if [ -d "$OUT_DIR" ]; then
        printf 'internal=%s\noneInTwoOut=%s\ntwoInOneOut=%s\ntotal_non3out=%s\n' "$internal" "$oneIn" "$twoIn" "$total" > "$OUT_DIR/triangles_non3out.txt"
        echo "  triangles_non3out.txt: total_non3out=$total"
    fi
done
echo "========================================"
