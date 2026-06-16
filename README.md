# rop-algorithm

Go + C++ optimization engine for vehicle routing with time windows (HFVRPTWB).

---

## Development Workflow

### Dev Flow — daily work

`rop-backend` uses Go `replace` directive to link against local `rop-algorithm`. Clone both repos side by side:

```bash
git clone https://github.com/ROP-TEAM/rop-algorithm.git
git clone https://github.com/ROP-TEAM/rop-backend.git
cd rop-backend && go build ./...
# replace => ../rop-algorithm resolves automatically
```

Changes here are visible in rop-backend instantly — no tag needed.

### Release Flow — production

```bash
git tag v0.1.0
git push origin v0.1.0
```

CI (`.github/workflows/release-go.yml`) runs tests + creates GitHub Release automatically. `rop-backend`'s release workflow fetches the latest tag.

## CI

| Workflow | Trigger | Action |
|---|---|---|
| `build-solver.yml` | push main/dev, PR | Build C++ solver binary |
| `release-go.yml` | push tag `v*` | Test + vet → GitHub Release |

---

## โครงสร้าง Module

```
rop-algorithm/
├── osrm/                         — package osrm: OSRM data layer (implements gmap.DistanceMatrix)
│   ├── matrix_service.go         — OSRMMatrix struct, NewOSRMMatrix, BuildMatrix
│   ├── matrix_endpoints.go       — Nearest, Trip, Route, BuildRectangularMatrix
│   ├── matrix_http.go            — HTTP execution, response parse, coordinate format
│   ├── matrix_response.go        — OSRM response types + public result types
│   └── readme.md                 — OSRM API docs
│
├── gmap/                         — package gmap: Google Maps data layer (nodes + edges)
│   ├── matrix_service.go         — GoogleMapsMatrix, ExecuteMatrix, BuildMatrix, orchestration
│   ├── matrix_request.go         — query building, validation, location normalization
│   ├── matrix_http.go            — HTTP execution, response decode, API checks
│   ├── matrix_cache.go           — cache policy, key, TTL logic
│   ├── matrix_cache_memory.go    — MatrixCache interface, MemoryMatrixCache
│   ├── matrix_observability.go   — MatrixEventHook, MatrixMetricsCollector, MatrixMetrics
│   ├── matrix_compat.go          — type aliases re-exporting จาก model/
│   └── readme.md
│
├── model/                        — data shapes only (ไม่มี behavior)
│   ├── matrix.go                 — Location, MatrixOptions, DistanceMatrix{Request,Result,Response,…}
│   ├── matrix_cache.go           — CachePolicy, MatrixCacheConfig, MatrixCacheKeyParts
│   ├── matrix_event.go           — MatrixEvent
│   ├── node.go                   — Node, NodeType (depot / delivery / pickup)
│   ├── vehicle.go                — Vehicle (capacity, shift/break windows, tags)
│   ├── problem.go                — Problem (depot + nodes + vehicles + n×n matrices)
│   └── solution.go               — Solution, Route, RouteStop, SolutionStatus
│
├── solver/                       — Solver interface + implementations
│   ├── main.go                   — Solver interface, StubSolver
│   ├── proto/
│   │   ├── routing.proto         — gRPC contract (SolverService.Solve)
│   │   └── generate.go           — go:generate directive for protoc
│   ├── grpc/
│   │   ├── client.go             — GRPCSolver: wraps pb.SolverServiceClient
│   │   ├── convert.go            — problemToProto, responseToSolution, flattenMatrix
│   │   ├── pb/                   — generated: solver.pb.go + solver_grpc.pb.go
│   │   └── README.md             — C++ implementation guide ← อ่านนี้ก่อน implement solver
│   └── process/
│       └── process.go            — Start(binaryPath) → Handle{Conn, Stop()}
│
├── graph/                        — package graph: placeholder (Phase 2)
│   └── pathFinder.go
├── core/                         — constraint, priority, time window: placeholder (Phase 2)
└── test/
    ├── matrix_test.go            — integration + HTTP + query-building tests
    └── matrix_cache_test.go      — cache key/TTL/policy unit tests
```

Layer rule: **osrm / gmap** = distance matrix data · **model** = data shapes · **solver** = interface + implementations · **test** = verification

---

## Commands

```bash
cd rop-algorithm
go build ./...
go test ./...
go vet ./...
```

---

## Solver Architecture

ใช้ gRPC subprocess pattern — Go spawns C++ binary บน free TCP port แล้วเชื่อมต่อผ่าน gRPC:

```
rop-backend
    │  SOLVER_BINARY_PATH set?
    ├─ yes → solverprocess.Start() → grpcsolver.New()  ← calls C++ over gRPC
    └─ no  → solver.NewStub()                          ← returns all unassigned (dev/test)
```

`Solver` interface ใน `solver/main.go`:

```go
type Solver interface {
    Solve(ctx context.Context, p model.Problem) (model.Solution, error)
}
```

สำหรับรายละเอียดการ implement C++ server ดูที่ → [solver/grpc/README.md](solver/grpc/README.md)

---

## Data Model

ทุก time field ใช้ **minutes from midnight** (int):

| เวลา | ค่า |
|------|-----|
| 08:00 | 480 |
| 12:30 | 750 |
| 17:00 | 1020 |

`model.Problem` ที่ส่งเข้า solver:

```go
type Problem struct {
    Depot     Node        // depot node (index 0 ใน matrix)
    Nodes     []Node      // delivery/pickup nodes (index 1…n)
    Vehicles  []Vehicle
    Durations [][]float64 // minutes,  Durations[i][j] = i→j
    Distances [][]float64 // meters,   Distances[i][j] = i→j
}
```

Matrix dimensions: `(1 + len(Nodes)) × (1 + len(Nodes))`

---

## Distance Matrix Providers

Two implementations of `gmap.DistanceMatrix`:

| Provider | Package | When to use |
|---|---|---|
| Google Maps | `gmap` | Production with real traffic data |
| OSRM | `osrm` | Self-hosted, no API cost, offline capable |

```go
// OSRM — self-hosted, no API key
m, _ := osrm.NewOSRMMatrix("http://localhost:5000")
durations, distances, _ := m.BuildMatrix(ctx, locs, model.MatrixOptions{})

// Google Maps — real traffic
m, _ := gmap.NewGoogleMapsMatrix(apiKey, gmap.WithInMemoryCache(cfg))
durations, distances, _ := m.BuildMatrix(ctx, locs, opts)
```

→ [gmap/readme.md](gmap/readme.md) — Google Maps API, cache, traffic
→ [osrm/readme.md](osrm/readme.md) — OSRM API, all endpoints

---
