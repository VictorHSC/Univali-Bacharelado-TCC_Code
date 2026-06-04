# CLAUDE.md

Guidance for Claude Code when working in this repository.

## What this is

A C++17 program that solves the **Container Ship Stowage Planning Problem (CSPP)**: given a
vessel and a loadlist, assign each container to a cell+slot so the ship stays seaworthy and
port efficiency is maximised. The objective is a weighted sum of 9 KPIs.

The search algorithm is **pluggable** behind a common `Algorithm` interface. Two are
included today:
- `csa` — Crow Search Algorithm (population metaheuristic), the default.
- `greedy` — a single-pass greedy construction, trivial baseline.

For the full user-facing docs (build, run, structure, how to add an algorithm) see
**`README.md`**. To build and run from a request, use the **`run-cspp`** skill.

## Build & run

```bash
./build.sh        # single clang++ command -> build/cspp (no CMake/Make; works from any path)
./build/cspp [--algo NAME] <vessel-file> <instance-file> [population] [max_iter] [threads]
./build/cspp --help        # the "Available:" line lists every registered algorithm
```

## Layout

```
src/
├── main.cpp        # CLI -> RunConfig -> Algorithm::solve -> print
├── model/          # pure data: Vessel, Container/Loadlist, Solution (KPIs)
├── io/             # VesselParser, InstanceParser
├── problem/        # the problem "physics" (algorithm-agnostic):
│                   #   Evaluator (9 KPIs + stability penalties), Repair, Stability
└── algorithm/      # the pluggable search:
                    #   Algorithm (interface), Factory, RunConfig, crow_search/, baseline/
```

Mental model: `model/` + `io/` + `problem/` describe the **problem** (identical for any
algorithm); `algorithm/` holds the **search**. A new algorithm only touches `algorithm/`.

## Key things to know when editing

- **`Solution::objective()`** = the 9-KPI weighted sum (the comparable score).
  **`Solution::fitness()`** = `objective() + stability/lashing penalties` (search guidance).
  Use `fitness()` to compare candidates during search; report `objective()`.
- After mutating a `Solution`, call **`Evaluator::update(solution, vessel, loadlist)`** to
  recompute KPIs and penalties.
- **`Repair::buildGreedy(...)`** gives a feasible starting `Solution`; neighbourhood moves
  live in `problem/Repair.hpp` and `algorithm/crow_search/Flight.hpp`.
- To register a new algorithm: subclass `Algorithm`, implement `solve`, add two lines in
  `algorithm/Factory.cpp` (`create()` + `available()`), rebuild. `main.cpp` and `--help`
  pick it up automatically. (Step-by-step in `README.md`.)