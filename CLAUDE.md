# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Commands

### Go

```bash
go build ./...
go test ./...
go vet ./...

# Run a single test
go test ./test/... -run TestName -v
go test ./core/priority/... -run TestName -v
```

### C++ Solver (core/)

```bash
cd core

# First build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build                          # builds solver + demo_solve
cmake --build build --target solver          # gRPC server only
cmake --build build --target demo_solve      # mock-data demo only

# Subsequent rebuilds after editing existing files
cmake --build build

# After adding a new .cpp file — update CMakeLists.txt first, then:
cmake -B build && cmake --build build

# Standalone clustering demo (no gRPC dependency):
cd core/priority_shape_clustering
cmake -B build && cmake --build build        # builds: build/demo
```

### Regenerate Go proto bindings (after editing solver/proto/solver.proto)

```bash
cd rop-algorithm
go generate ./solver/proto/
# Writes: solver/grpc/pb/solver.pb.go  solver/grpc/pb/solver_grpc.pb.go
```

On Windows, protoc and plugin paths are set in `solver/proto/generate.go`.

## Architecture

### Go/C++ Split

The module is a hybrid: Go handles data marshaling, Google Maps API, and the gRPC client. C++ handles the NP-hard routing optimization. The two sides communicate over a local gRPC connection.

### Layer Rules

| Layer | Responsibility |
|---|---|
| `model/` | Data shapes only — no behavior, no I/O |
| `gmap/` | Google Maps Distance Matrix API |
| `solver/` | `Solver` interface + `StubSolver` + gRPC client wrapper |
| `solver/process/` | Spawns/kills the C++ subprocess |
| `core/` | Constraint checking, priority sorting, time-window math |
| `test/` | Unit + integration tests (no test files live in package dirs) |

Nothing in `model/` may import from any other package in this module. `gmap/` may import `model/`. `solver/` may import `model/`.

### gRPC Subprocess Pattern

```
caller sets SOLVER_BINARY_PATH
    │
    ├─ empty → solver.NewStub()          returns all nodes unassigned / INFEASIBLE
    │
    └─ set   → process.Start(path)
                  • finds a free TCP port
                  • exec: ./solver 127.0.0.1:<port>
                  • polls TCP every 50 ms, 10 s timeout
                  • opens insecure gRPC conn
               → grpcsolver.New(conn)    calls C++ over gRPC
```

`process.Handle.Stop()` closes the gRPC connection and kills the subprocess. The `Solver` interface (`solver/main.go`) is the only surface the rest of the system touches — the stub and gRPC implementations are interchangeable.

### C++ Solver Internals (core/priority_shape_clustering/)

Algorithm pipeline per round:

1. **Priority sort** — deadline ASC → TWEnd ASC → priority DESC
2. **k-medoids clustering** — evenly-spaced center seeds across sorted candidates; assigns each node to nearest center
3. **MST per cluster** — Prim's algorithm builds a minimum spanning tree over cluster nodes
4. **DFS tour** — traverses MST to produce a visit order
5. **2-opt** — local search pass over each cluster's route

The C++ binary listens on the address passed as `argv[1]`, implements `SolverService::Solve`, and runs until killed.

## Key Conventions

### Time representation

All times everywhere are **minutes from midnight** (integer). There are no clock strings at the solver boundary.

```
08:00 → 480    12:30 → 750    17:00 → 1020
```

`model.InputVehicle.ToVehicle()` and `model.InputOrder.ToNode()` parse `"HH:mm"` strings into this representation at the ingestion boundary.

### Matrix layout

`Problem.Durations` and `Problem.Distances` are square `(1+n)×(1+n)` matrices:
- Index 0 = depot
- Index 1…n = nodes[0]…nodes[n-1]

For gRPC they are flattened row-major: `element[i][j] = flat[i * matrix_size + j]`.

### Feasibility constraints (all 6 must hold on every route)

1. **Capacity** — sum(demands) ≤ vehicle capacity
2. **Time window** — `tw_start ≤ arrival ≤ tw_end`; early arrival waits, no penalty
3. **Shift window** — depart depot ≥ `shift_start`; return ≤ `shift_end`
4. **Break** — if `break_start > 0`, a break must occur within `[break_start, break_end]`
5. **Tag match** — node.tags ∩ vehicle.tags ≠ ∅; either list empty = no restriction
6. **PD pairs** — for nodes sharing a `pair_id`, pickup stop precedes delivery in the same route

**Scoring:** objective must include `1000 × len(unassigned)` so full assignment always dominates distance/lateness minimisation.

### DeadlineMin

`Node.DeadlineMin` = minutes from planning midnight; `0` = no deadline. Multi-day deadlines continue counting past 1439 (e.g. tomorrow 17:00 = 2460). Nodes with `DeadlineMin == 0` sort last in `core/priority.SortNodes` (treated as MaxInt32).

## What Is Not Yet Implemented (Phase 2)

- `graph/pathFinder.go` — placeholder, no logic
- `core/constraint/checker.go` — placeholder, no logic
- `buildProblem()` / `saveSolution()` in `rop-backend` — the bridge between GORM models and `model.Problem`/`model.Solution`

### C++ solver constraint gaps

`Node` proto fields received but currently **ignored** in `core/`: `tw_end`, `tags`, `type`, `pair_id`.
`Vehicle` proto fields received but currently **ignored**: `shift_end`, `break_start`, `break_end`, `max_distance`, `tags`.

| Constraint | Status |
|---|---|
| Capacity | ✅ enforced in `kMedoidsIterate` |
| tw_start (early wait) | ✅ applied in route building |
| max_tasks per vehicle | ✅ enforced in `kMedoidsIterate` |
| tw_end (latest arrival) | ❌ ignored — late arrivals not unassigned |
| shift_end (vehicle return) | ❌ ignored |
| Break window | ❌ ignored |
| Tag match (node ∩ vehicle) | ❌ ignored — incompatible nodes assigned freely |
| PD pairs (pickup before delivery) | ❌ not enforced |
| max_distance per vehicle | ❌ ignored |
| Penalty scoring (1000 × unassigned) | ❌ not applied |

## Reference Docs

Detailed usage for sub-packages lives alongside the code:

- `gmap/readme.md` — caching strategy, traffic options, chunking, error handling
- `solver/grpc/README.md` — full proto contract, C++ server skeleton, testing without C++
