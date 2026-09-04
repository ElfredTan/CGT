#!/bin/bash
# ============================================================
# runall_GTcontract.sh (CGT 迁移版)
# 生成收缩图实际节点 GT 编码: output_<ds>/gt_input_edges.txt -> GTBIN_contract/<ds>.bin
# 用法: Graphbit0 <mode> <input> <output>, mode=2 生成, mode=1 校验; KL=2
# 用法:
#   ./runall_GTcontract.sh                (默认只跑 bk)
#   DATASETS_OVERRIDE="bk lj" ./runall_GTcontract.sh
# ============================================================
cd "$(dirname "$0")" || exit 1

[ -n "$DATASETS_OVERRIDE" ] && DATASETS=($DATASETS_OVERRIDE) || DATASETS=(
    "bk"
    # "citehepth"
    # "ctr"
    # "dblp"
    # "ep"
    # "gn"
    # "google"
    # "hollywood"
    # "lj"
    # "notredame"
    # "pokec"
    # "slash"
    # "standford"
    # "twitter"
    # "usa"
    # "wiki"
    # "youtube"
)

# ---- 编译 (统一走根级 Makefile) ----
make -s -C .. gt_encode || { echo "编译失败, 终止"; exit 1; }

PROGRAM="./Graphbit0"
KL=2
# CGT 数据目录 (../../data/output_<ds>/ 与 ../../data/gtbin/)
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

    # 使用 gt_input_edges.txt（收缩后图的边列表格式）
    INPUT_FILE="${INPUT_BASE_DIR}/output_${dataset}/gt_input_edges.txt"
    OUTPUT_FILE="${OUTPUT_BASE_DIR}/${dataset}.bin"

    if [ ! -f "$INPUT_FILE" ]; then
        echo "[ERROR] Input file not found: $INPUT_FILE" | tee -a "$LOG_FILE"
        echo "Available files:"
        ls -la "${INPUT_BASE_DIR}/output_${dataset}/" 2>&1 | tee -a "$LOG_FILE"
        continue
    fi

    # 验证文件格式
    FIRST_LINE=$(head -1 "$INPUT_FILE")
    echo "First line: $FIRST_LINE" | tee -a "$LOG_FILE"

    # 执行
    $PROGRAM $KL "$INPUT_FILE" "$OUTPUT_FILE" 2>&1 | tee "$LOG_FILE"

    if [ ${PIPESTATUS[0]} -eq 0 ]; then
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] SUCCESS: $dataset" | tee -a "$LOG_FILE"
    else
        echo "[$(date '+%Y-%m-%d %H:%M:%S')] FAILED: $dataset" | tee -a "$LOG_FILE"
    fi

    echo ""
done

echo "All tasks completed!"
