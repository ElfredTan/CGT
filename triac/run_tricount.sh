#!/bin/bash
# ============================================================
# run_tricount.sh (CGT/triac) — 三角形计数运行脚本
#
# 职责划分 (0905):
#   - 非三外三角形 (internal/一内二外/二内一外): 由 **收缩流程顺带离线完成**
#     (trioffline 算法), 结果保存于 ../data/output_<ds>/triangles_non3out.txt
#   - 本脚本: 运行 triac_gt 计三外三角形, 并读取 triangles_non3out.txt
#     汇总输出 总三角形 = 非三外 + 三外; 不重复运行离线非三外计数
#
# 用法:
#   ./run_tricount.sh
#   DATASETS_OVERRIDE="bk lj" ./run_tricount.sh
#   OFFLINE=1 ./run_tricount.sh       # 额外独立运行 trioffline 校验非三外
# ============================================================
cd "$(dirname "$0")" || exit 1

[ -n "$DATASETS_OVERRIDE" ] && DATASETS=($DATASETS_OVERRIDE) || DATASETS=(
    "bk"
)

# ---- 编译 (统一走根级 Makefile) ----
make -s -C .. triac_gt || { echo "编译失败, 终止"; exit 1; }
[ "${OFFLINE:-0}" = "1" ] && make -s -C .. trioffline || true

LOG_DIR="./logs_tricount"
mkdir -p "$LOG_DIR"

for dataset in "${DATASETS[@]}"; do
    echo "======== $dataset ========"
    LOG_FILE="${LOG_DIR}/gt_${dataset}.log"

    # ---- 三外三角形 (triac_gt) ----
    ./triac_gt "$dataset" > "$LOG_FILE" 2>&1
    tri3out=$(grep -oP 'triangleCount:\s*\K[0-9]+' "$LOG_FILE")
    echo "  三外 (triac_gt):        $tri3out"

    # ---- 非三外 (读取收缩顺带保存的结果) ----
    NON3_FILE="../data/output_${dataset}/triangles_non3out.txt"
    non3out=""
    if [ -f "$NON3_FILE" ]; then
        non3out=$(grep -oP '^total_non3out=\K[0-9]+' "$NON3_FILE")
    fi
    if [ -n "$non3out" ]; then
        echo "  非三外 (gcon 顺带):     $non3out"
        echo "  总三角形 (相加):        $((non3out + tri3out))"
    else
        echo "  [WARN] $NON3_FILE 不存在 — 先运行 gcon/run_gcon.sh 生成非三外计数"
        [ "${OFFLINE:-0}" = "1" ] && echo "  (OFFLINE=1 可独立运行 trioffline 校验)"
    fi
    echo ""
done

# ---- 可选: 独立运行 trioffline 校验非三外 ----
if [ "${OFFLINE:-0}" = "1" ]; then
    for dataset in "${DATASETS[@]}"; do
        LOG_FILE="${LOG_DIR}/offline_${dataset}.log"
        ./trioffline "$dataset" > "$LOG_FILE" 2>&1
        echo "offline 校验 $dataset: $(grep -oP '总计 \(total\): \K[0-9]+' "$LOG_FILE") (非三外)"
    done
fi
