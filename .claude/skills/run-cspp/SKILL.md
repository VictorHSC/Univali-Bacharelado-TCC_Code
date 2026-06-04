---
name: run-cspp
description: Build and run the CSPP container-ship stowage solver and report the result. Use whenever the user wants to build, compile, run, test, or benchmark the solver, pick an algorithm (the solver is pluggable - discover the registered ones at runtime), change parameters (population, iterations, threads), or run across vessel sizes (S/M/L), utilisation (High/Med/Low), or instances (1/2/3).
---

Build the binary if needed, run it with the requested options, and show the result.

All commands run from the project root (the folder that contains `src/` and `benchmark/`).

## Build

One command, works from any path:

```bash
./build.sh
```

(It runs the clang++ compile and writes `build/cspp`.)

Skip rebuilding if `build/cspp` exists and no source changed:
`find src -name '*.cpp' -newer build/cspp` (empty output = up to date).

If the build fails, show the error and stop - don't run.

## Run

```bash
./build/cspp [--algo NAME] <vessel-file> <instance-file> [population] [max_iter] [threads]
```

| Option     | Values                          | Default |
|------------|---------------------------------|---------|
| --algo     | a registered solver (see below) | csa     |
| vessel     | S, M, L                         | S       |
| util       | High, Med, Low                  | High    |
| instance   | 1, 2, 3                         | 1       |
| population | integer                         | 20      |
| max_iter   | integer                         | 1000    |
| threads    | integer (1 = deterministic)     | 4       |

**Algorithms are pluggable** - the solver is generic and new ones may be registered over
time. Never assume a fixed list. To get the current `--algo` values, read them from the
binary itself:

```bash
./build/cspp --help        # the "Available:" line lists every registered solver
```

If the user names an algorithm not in that list, say so and show the available ones.
`csa` is the default; `greedy` is a trivial single-pass baseline. The
population/iter/threads options only apply to population-based metaheuristics like `csa`
(a plain baseline ignores them) - if unsure whether a given solver uses them, just pass
them anyway; unused options are harmless.

Accept positional (`M Low 2 40 2000`), key=value (`vessel=M util=Low population=40`), or
plain language ("medium vessel, low utilisation, 40 population").

Map the friendly args to file paths:

```bash
./build/cspp \
  benchmark/vessels/vessel_<S|M|L>.txt \
  benchmark/instances/Vessel_<S|M|L>/V<S|M|L><High|Med|Low><1|2|3>.txt \
  <population> <max_iter> <threads>
```

Instance file naming: `V<vessel><util><n>` - e.g. `VSHigh1`, `VMMed2`, `VLLow3` (27 total:
3 vessels x 3 utilisations x 3 replicates).

## Report

Run it and show what the program prints (it already prints the objective, a KPI breakdown,
and a seaworthiness block). Then give a one-line summary:

```
VSHigh1 (csa, 20 pop, 1000 iter, 4 threads) -> objective 156577, unloaded 0, NOT SEAWORTHY (12s)
```

Always surface: the **objective**, **unloaded** count, and the **seaworthy** flag. Round
the objective to a whole number in the summary line. Use the numbers the program actually
prints - never invent values.

Notes:
- `threads=1` is deterministic (same result every run); `threads>1` varies slightly run to run.
- More `population` + `max_iter` = better result, more time. S is fast; M/L are slower.

## Sweep

If asked for multiple runs ("all 27", "sweep population 10-40"), loop and collect a table:

| Instance | Objective | Unloaded | Seaworthy | Time |
|----------|-----------|----------|-----------|------|
| VSHigh1  | 156577    | 0        | no        | 12s  |

For long sweeps, print a progress line after each run, e.g.
`[3/27] VMMed1 - objective 188758 (82s)`.

**Save the results.** Write the collected table to the `results/` folder (create it if
missing) so the run is kept on disk, then show the same table in the reply:

```bash
mkdir -p results
```

Use a descriptive, timestamped filename, e.g.
`results/sweep-all27-csa-20pop-1000iter-2026-06-04.md`, and put the run settings
(algorithm, population, iterations, threads) in a header line inside the file. The
`results/` folder is git-ignored — these are regenerable outputs, not committed.
