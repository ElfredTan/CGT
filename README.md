# CGT

A compact artifact repository for the paper *Cache-Conscious Edge Oracle for Fast and Exact Graph Queries*: graph contraction → GT encoding → three downstream algorithms (subac / cdac / triac).

## Repository Layout

```
Makefile            Unified build with -O3 optimization; MODE=gt|gtwithcount
contract/           Contraction algorithm     → data/output_<ds>/
gt_encode/          GT encoder                → data/gtbin/<ds>.bin
common/             Shared graph-loading layer for the three algorithms
subac/ cdac/ triac/ The three downstream algorithms
data/               raw/ + output_<ds>/ + gtbin/
```

## Environment

Linux, g++ ≥ 9 (C++17). Everything is built with `-O3` (see the Makefile). Source files also use `#pragma GCC optimize`, which is GCC-specific and ignored by clang; build with g++.

## Quick Start

```bash
make
mkdir -p data/raw/test_data                                   # place raw graphs here
cd contract  && DATASETS_OVERRIDE="BK" ./run_gcon.sh
cd gt_encode && DATASETS_OVERRIDE="BK" ./run_gt_encode.sh
cd subac     && ./run_subac.sh                               # six queries (5 runs each)
cd cdac      && ./run_cdac.sh                                 # decision over a sequence of k values
cd triac     && DATASETS_OVERRIDE="BK" ./run_tricount.sh
```

- **Input data**: place each raw graph as `data/raw/test_data/<DATASET>.txt` (format below). Contraction reads that directory by default; set `GT_DATA_DIR=/path/to/your/graphs` to point it anywhere else (files inside are `<DATASET>.txt`).
- **Repetitions**: the three downstream algorithms default to 5 repetitions, time averaged (pass a number to override): `./run_subac.sh 3`, `./run_cdac.sh coloring gtwithcount 3`, `./run_tricount.sh 3`.
- **k values (cdac)**: a few steps around the maximum clique of the original graph, centered on ω and ω+1; the step grows with the clique size. Extending leftward, the sequence adjusts automatically when it reaches the max *contracted* clique size (the largest clique contraction finds, usually smaller than ω).

The six query graphs (subac) are hardcoded in `subac/subac.cpp`; the per-dataset k sequences (cdac) live in `cdac/run_cdac.sh`. See those files for the exact structures.

## Input Graph Format

Plain-text file:

```
node_num edge_num
0 2
0 3
1 4
```

Vertices are numbered consecutively starting from 0, with no self-loops or duplicate edges.

Very large graphs (e.g. IT) may alternatively be kept in binary CSR form; the GT encoder (`gt_encode`) accepts plain text as well as `.csr`/`.bin` inputs.

## Datasets

16 graphs, referred to by the abbreviations below throughout the repository (directory names, scripts, outputs).

| Abbr | Full name | Source |
|------|-----------|--------|
| BK   | BerkStan      | SNAP |
| CH   | citeHepth     | SNAP |
| CTR  | CTR           | DIMACS 9th Implementation Challenge ([download](https://www.diag.uniroma1.it/challenge9/download.shtml)) |
| DB   | DBLP          | SNAP |
| EP   | Epinions      | SNAP |
| GO   | Google        | SNAP |
| HW   | Hollywood     | [Network Repository](https://networkrepository.com/) |
| IT   | IT            | Laboratory for Web Algorithms ([datasets](https://law.di.unimi.it/datasets.php)) |
| LJ   | LiveJournal   | SNAP ([data](https://snap.stanford.edu/data/)) |
| ND   | NotreDame     | SNAP |
| SP   | Livepokec     | SNAP |
| SD   | Slashdot      | SNAP |
| SF   | Standford     | SNAP |
| TW   | Twitter       | SNAP |
| USA  | USA           | DIMACS 9th Implementation Challenge ([download](https://www.diag.uniroma1.it/challenge9/download.shtml)) |
| WT   | WikiTalk      | SNAP |

SNAP: [snap.stanford.edu/data](https://snap.stanford.edu/data/).
