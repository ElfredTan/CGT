#!/bin/bash
# ============================================================
# runall_contract.sh (CGT 迁移版)
# 批量收缩: 原始图 -> ../output_<dataset>/
# 来自 contractgraph/src_ref0817bfbak/runall_contract_0707.sh
#   (path 成员顺序修复版; main 输出 ../output_<ds>/, 输入
#    /data/tanjinlin/GTdata/test_data/<ds>.txt)
# 用法:
#   ./runall_contract.sh                (默认只跑 bk)
#   DATASETS_OVERRIDE="bk lj" ./runall_contract.sh
# ============================================================
cd "$(dirname "$0")" || exit 1

# ===== 可配置参数 =====
# KU: 收缩上界; SHAPES: 形状顺序 (注: 当前 main.cpp 忽略 argv[3], 仅保留接口)
KU=500
SHAPES=""

[ -n "$DATASETS_OVERRIDE" ] && DATASETS=($DATASETS_OVERRIDE) || DATASETS=(
    "bk"
    # ""
)

# ---- 编译 (统一走根级 Makefile) ----
echo "[$(date '+%H:%M:%S')] 编译 contract (make contract) ..."
make -s -C .. contract || { echo "编译失败, 终止"; exit 1; }
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

echo "Logs: $LOG_DIR"

# ===== 收缩顺带 - 非三外三角形计数 (trioffline 口径) =====
# 结果写入 ../data/output_<ds>/triangles_non3out.txt, 供 triac/run_tricount.sh 读取汇总
make -s -C .. trioffline || { echo "triac_baseline build fail"; exit 1; }
for dataset in "${SUCCESS[@]}"; do
    echo "[$(date '+%H:%M:%S')] 非三外三角形计数: $dataset ..."
    ( cd ../triac && ./trioffline "$dataset" > /tmp/tri_non3out_${dataset}.log 2>&1 )
    internal=$(grep -oP "internal\\): \\K[0-9]+" /tmp/tri_non3out_${dataset}.log)
    oneIn=$(grep -oP 'oneInTwoOut\): \K[0-9]+' /tmp/tri_non3out_${dataset}.log)
    twoIn=$(grep -oP 'twoInOneOut\): \K[0-9]+' /tmp/tri_non3out_${dataset}.log)
    total=$(grep -oP 'total\): \K[0-9]+' /tmp/tri_non3out_${dataset}.log)
    OUT_DIR="../data/output_${dataset}"
    if [ -d "$OUT_DIR" ]; then
        printf 'internal=%s\noneInTwoOut=%s\ntwoInOneOut=%s\ntotal_non3out=%s\n' "$internal" "$oneIn" "$twoIn" "$total" > "$OUT_DIR/triangles_non3out.txt"
        echo "  triangles_non3out.txt: total_non3out=$total"
    fi
done
echo "========================================"
