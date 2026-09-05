# ============================================================
# CGT root Makefile - unified build (all -O3)
#
# MODE, two variants (paper convention, repo-wide):
#   gt           GT on, edge-check counters not recorded
#   gtwithcount  GT on, edge-check counters recorded
#
# Usage:
#   make                 # all targets (MODE=gt)
#   make contract gt_encode trioffline triac_gt
#   make cdac MODE=gtwithcount   # NOTE: MODE is a compile-time macro and the Makefile
#                                # does not track MODE changes - run `make clean` when
#                                # switching MODE on the command line. The run scripts
#                                # remove the binary before building, so they are safe.
#
# TRIALS (repetitions, default 5) applies to subac and cdac: their run scripts
# take it as a positional argument at runtime; it is independent of the build.
# ============================================================

MODE ?= gt
CXX ?= g++

COMMON = common/graph.cpp common/graph.h

# ---- MODE -> macro mapping ----
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

# The two triangle-counting variants are independent targets (baseline / GT), not MODE-switched
trioffline: triac/trioffline
triac/trioffline: triac/trioffline.cpp $(COMMON) triac/trioffline.h
	$(CXX) -O3 -Icommon -o $@ triac/trioffline.cpp common/graph.cpp

ifeq ($(MODE),gtwithcount)
  TRIAC_GT_DEFS = -DRECORD_EDGE_STATS=1
else
  TRIAC_GT_DEFS =
endif

triac_gt: triac/triac_gt
triac/triac_gt: triac/triac_gt.cpp $(COMMON) triac/triac_gt.h
	$(CXX) -O3 -Icommon $(TRIAC_GT_DEFS) -o $@ triac/triac_gt.cpp common/graph.cpp

# Contraction: C++17 (structured bindings); writes ../data/output_<ds>/
contract: contract/gcon
contract/gcon: contract/main.cpp contract/contraction_core.cpp contract/Graph.h
	$(CXX) -O3 -std=c++17 -o $@ contract/main.cpp contract/contraction_core.cpp

gt_encode: gt_encode/gt_encoder
gt_encode/gt_encoder: gt_encode/gt_encoder.cpp
	$(CXX) -O3 -o $@ gt_encode/gt_encoder.cpp

clean:
	rm -f subac/subac cdac/cdac triac/trioffline triac/triac_gt contract/gcon gt_encode/gt_encoder
