# CGT

A compact artifact repository for the paper *Cache-Conscious Edge Oracle for Fast and Exact Graph Queries*: graph contraction → GT encoding → three downstream algorithms (subac / cdac / triac).

## Repository Layout

```
Makefile            Unified build with -O3 optimization; MODE=gt|gtwithcount
contract/           Contraction algorithm     → data/output_<ds>/
gt_encode/          GT encoder                → data/gtbin/<ds>.bin
common/             Shared graph-loading layer for the three algorithms
subac/ cdac/ triac/ The three downstream algorithms
data/               raw/ (symlinks to raw graphs) + output_<ds>/ + gtbin/

## Quick Start

```bash
make                                          # Build everything (default MODE=gt)
cd contract  && DATASETS_OVERRIDE="bk" ./run_contract.sh
cd gt_encode && DATASETS_OVERRIDE="bk" ./run_gt_encode.sh
cd subac     && MODE=gtwithcount ./run_subac.sh          # six queries
cd cdac      && ./run_cdac.sh                            # decision over a sequence of k values
cd triac     && DATASETS_OVERRIDE="bk" ./run_tricount.sh
```

- **MODE**: `gt` (enable GT without counting edge queries) / `gtwithcount` (enable GT and record edge queries).
- **Repetitions**: `TRIALS=5 ./run_subac.sh` (default 5; reported time is averaged over `TRIALS` runs, counters are taken from the last run).

## Input Graph Format

Plain-text file:

```
node_num edge_num
0 2
0 3
1 4
```

Vertices are numbered consecutively starting from 0, with no self-loops or duplicate edges. Large graphs are read in binary CSR format.
