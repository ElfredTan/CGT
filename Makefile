# ============================================================
# CGT 根级 Makefile — 统一编译口径 (全部 -O3)
#
# MODE 两模式 (论文口径, 全仓库统一):
#   gt           GT 开, 不记录查边计数器        (旧名 noedge)
#   gtwithcount  GT 开, 记录查边计数器          (旧名 default)
#
# 用法:
#   make                 # 全部 target (MODE=gt)
#   make cdac MODE=gtwithcount
#   make contract gt_encode trioffline triac_gt
#
# TRIALS (重复次数, 默认 5) 由运行脚本以环境变量传入源码第 4 参, 编译期无关。
# ============================================================

MODE ?= gt
CXX ?= g++

COMMON = common/graph.cpp common/graph.h

# ---- 模式 → 宏映射 ----
ifeq ($(MODE),gt)
  SUBAC_DEFS  = -DNO_GT_STATS
  CDAC_DEFS   = -DUSE_GT=1 -DRECORD_EDGE_STATS=0
else ifeq ($(MODE),gtwithcount)
  SUBAC_DEFS  =
  CDAC_DEFS   = -DUSE_GT=1 -DRECORD_EDGE_STATS=1
else
  $(error Unknown MODE: $(MODE) (use: gt | gtwithcount))
endif

.PHONY: all subac cdac trioffline triac_gt contract gt_encode clean

all: subac cdac trioffline triac_gt contract gt_encode

subac: subac/subac
subac/subac: subac/subac.cpp $(COMMON) subac/turboiso.h
	$(CXX) -O3 -Icommon $(SUBAC_DEFS) -o $@ subac/subac.cpp common/graph.cpp

cdac: cdac/cdac
cdac/cdac: cdac/cdac.cpp $(COMMON) cdac/cdac.h
	$(CXX) -O3 -Icommon $(CDAC_DEFS) -o $@ cdac/cdac.cpp common/graph.cpp

# 三角形计数两个变体是独立 target (非 GT / GT), 不按 MODE 切换
trioffline: triac/trioffline
triac/trioffline: triac/trioffline.cpp $(COMMON) triac/trioffline.h
	$(CXX) -O3 -Icommon -o $@ triac/trioffline.cpp common/graph.cpp

triac_gt: triac/triac_gt
triac/triac_gt: triac/triac_gt.cpp $(COMMON) triac/triac_gt.h
	$(CXX) -O3 -Icommon -o $@ triac/triac_gt.cpp common/graph.cpp

# 收缩: C++17 (结构化绑定); 输出 ../data/output_<ds>/
contract: contract/gcon
contract/gcon: contract/main.cpp contract/contraction_core.cpp contract/Graph.h
	$(CXX) -O3 -std=c++17 -o $@ contract/main.cpp contract/contraction_core.cpp

gt_encode: gt_encode/Graphbit0
gt_encode/Graphbit0: gt_encode/Graphbit0.cpp
	$(CXX) -O3 -o $@ gt_encode/Graphbit0.cpp

clean:
	rm -f subac/subac cdac/cdac triac/trioffline triac/triac_gt contract/gcon gt_encode/Graphbit0
