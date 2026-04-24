# gRPC Solver — Implementation Guide

This document describes the gRPC contract between the Go backend and the C++ ALNS solver, and explains how to implement the server side.

---

## How It Works

```
rop-backend
    │
    │  spawns subprocess with address argument
    ▼
solver binary  ←──── solver/process/process.go
    │                  • picks a free TCP port
    │                  • runs: ./solver 127.0.0.1:<port>
    │                  • polls TCP until ready (10 s timeout)
    │                  • opens gRPC client connection
    ▼
SolverService (gRPC server)
    │  Solve(SolveRequest) → SolveResponse
    ▼
ALNS algorithm
```

The binary receives a single argument — the address to listen on (e.g. `127.0.0.1:52341`). It must start a gRPC server on that address, implement `SolverService.Solve`, then keep running until killed.

---

## Proto Contract

File: [`solver/proto/solver.proto`](../proto/solver.proto)

### Input — `SolveRequest`

| Field | Type | Description |
|---|---|---|
| `depot` | `Node` | Starting/ending location for all vehicles |
| `nodes` | `Node[]` | All delivery/pickup stops (excludes depot) |
| `vehicles` | `Vehicle[]` | Available vehicles |
| `durations` | `double[]` | Flattened travel-time matrix, **minutes** |
| `distances` | `double[]` | Flattened travel-distance matrix, **meters** |
| `matrix_size` | `int32` | `n = 1 + len(nodes)` (depot counts as index 0) |
| `time_limit_ms` | `int32` | Solver time limit in milliseconds (0 = no limit) |

### `Node` fields

| Field | Type | Notes |
|---|---|---|
| `id` | `string` | Unique identifier |
| `lat`, `lng` | `double` | WGS-84 coordinates |
| `demand` | `int32` | Capacity consumed from vehicle |
| `service_time` | `int32` | Minutes spent at this stop |
| `tw_start`, `tw_end` | `int32` | Earliest/latest arrival, **minutes from midnight** |
| `tags` | `string[]` | Required skill tags; must intersect vehicle tags |
| `type` | `string` | `depot` / `delivery` / `pickup` |
| `pair_id` | `string` | Links pickup↔delivery pairs; empty if not a PD pair |

### `Vehicle` fields

| Field | Type | Notes |
|---|---|---|
| `id` | `string` | Unique identifier |
| `capacity` | `int32` | Max load (sum of demands on route) |
| `shift_start`, `shift_end` | `int32` | Working hours, **minutes from midnight** |
| `break_start`, `break_end` | `int32` | Break window; `break_start = 0` means no break |
| `max_tasks` | `int32` | Max stops on the route; `0` = unlimited |
| `max_distance` | `double` | Max route distance in **meters**; `0` = unlimited |
| `tags` | `string[]` | Vehicle capabilities; must intersect node tags to serve that node |

### Output — `SolveResponse`

| Field | Type | Description |
|---|---|---|
| `routes` | `Route[]` | One entry per vehicle that has at least one stop |
| `unassigned` | `string[]` | Node IDs that could not be assigned |
| `objective` | `double` | Objective value (lower = better) |
| `status` | `string` | `OK` / `INFEASIBLE` / `TIMEOUT` |

`Route`:

| Field | Type | Notes |
|---|---|---|
| `vehicle_id` | `string` | Matches a vehicle `id` from the request |
| `stops` | `RouteStop[]` | Ordered sequence of stops (depot excluded) |
| `total_distance` | `double` | Accumulated route distance, **meters** |
| `total_duration` | `int32` | Accumulated route duration, **minutes** |

`RouteStop`:

| Field | Type | Notes |
|---|---|---|
| `node_id` | `string` | Matches a node `id` from the request |
| `arrival_min` | `int32` | Actual arrival time, **minutes from midnight** |
| `depart_min` | `int32` | Departure time = `arrival_min + service_time` |

---

## Matrix Layout

Both `durations` and `distances` are square matrices flattened **row-major** into a 1-D array.

```
Index 0        = depot
Index 1 … n-1  = nodes[0] … nodes[n-2]
matrix_size    = n  (= 1 + len(nodes))

Element [i][j] = durations[ i * matrix_size + j ]
```

Example — 3 nodes + 1 depot → `matrix_size = 4`, array length = 16:

```
     depot  n0    n1    n2
depot [  0,  5.2,  8.1,  3.4 ]
n0   [  5.2, 0,   2.3,  6.0 ]
n1   [  8.1, 2.3,  0,   4.5 ]
n2   [  3.4, 6.0,  4.5,  0  ]
```

To reconstruct a 2-D matrix in C++:

```cpp
int n = request.matrix_size();
auto& flat = request.durations();  // repeated double, size = n * n

auto duration = [&](int from, int to) {
    return flat[from * n + to];
};
```

---

## Time Convention

All times throughout the system are **minutes from midnight** (integer).

| Clock | Value |
|---|---|
| 00:00 | 0 |
| 08:00 | 480 |
| 12:30 | 750 |
| 17:00 | 1020 |
| 23:59 | 1439 |

---

## Feasibility Constraints

The solver must enforce all 6 constraints on every route it produces:

1. **Capacity** — sum of `demand` across all stops ≤ vehicle `capacity`
2. **Time window** — arrival at node `i` must satisfy `tw_start ≤ arrival ≤ tw_end`; early arrival waits until `tw_start`
3. **Shift window** — vehicle departs depot no earlier than `shift_start`; arrives back no later than `shift_end`
4. **Break** — if `break_start > 0`, vehicle must take a break in `[break_start, break_end]`
5. **Tag match** — at least one of the node's `tags` must appear in the vehicle's `tags`; if either list is empty, no restriction applies
6. **PD pairs** — for nodes with the same `pair_id`, the pickup stop must appear before the delivery stop in the same route

**Scoring penalty:** the objective must add `1000 × len(unassigned)` so that assigning all orders always dominates minimising distance or lateness.

---

## Implementing the Server (C++)

### 1. Generate C++ stubs from the proto

```bash
protoc --cpp_out=. --grpc_out=. \
       --plugin=protoc-gen-grpc=$(which grpc_cpp_plugin) \
       solver.proto
# Produces: solver.pb.h  solver.pb.cc  solver.grpc.pb.h  solver.grpc.pb.cc
```

### 2. Implement `SolverService`

```cpp
#include "solver.grpc.pb.h"
#include <grpcpp/grpcpp.h>

class SolverServiceImpl final : public solver::SolverService::Service {
public:
    grpc::Status Solve(
        grpc::ServerContext*,
        const solver::SolveRequest* req,
        solver::SolveResponse* resp
    ) override {
        int n = req->matrix_size();

        // Reconstruct matrix helpers
        auto dur = [&](int i, int j) { return req->durations(i * n + j); };
        auto dist = [&](int i, int j) { return req->distances(i * n + j); };

        // Run your ALNS algorithm here, populate resp->mutable_routes() etc.
        // ...

        resp->set_status("OK");
        resp->set_objective(total_cost);
        return grpc::Status::OK;
    }
};
```

### 3. Start the server on the address argument

```cpp
int main(int argc, char** argv) {
    if (argc < 2) { std::cerr << "usage: solver <addr>\n"; return 1; }
    std::string addr = argv[1];  // e.g. "127.0.0.1:52341"

    SolverServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);

    auto server = builder.BuildAndStart();
    server->Wait();
}
```

The Go side polls the address every 50 ms for up to 10 seconds. The server must be accepting TCP connections before `server->Wait()` blocks — `BuildAndStart()` does this automatically.

---

## Testing Without C++

With `SOLVER_BINARY_PATH` unset (the default), the backend uses `solver.NewStub()` which returns all nodes as `unassigned` with status `INFEASIBLE`. This lets the entire pipeline compile and run without a C++ binary.

To test the gRPC path, set the env var before starting the backend:

```bash
# .env or shell
SOLVER_BINARY_PATH=/path/to/solver_binary
```

---

## Regenerating Go Proto Code

If you edit `solver.proto`, regenerate the Go bindings:

```bash
cd rop-algorithm

# Windows — add protoc to PATH first
export PATH="$PATH:/path/to/protoc/bin"

go generate ./solver/proto/
# Writes: solver/grpc/pb/solver.pb.go
#         solver/grpc/pb/solver_grpc.pb.go
```
