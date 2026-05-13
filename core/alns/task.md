# Phase 4 ALNS — Implementation Tasks

## Architecture

**Without ALNS** (current): `adaptiveConstruct → applyTwoOpt → orOptRelocate → writeResponse`

**With ALNS**: `adaptiveConstruct → ALNSSolver (calls twoOpt+orOpt internally) → writeResponse`

ALNS **replaces** the standalone `applyTwoOpt` + `orOptRelocate` calls — don't run both.

Gated by `cfg_.enableALNS` in `solver_service.cpp`.

`routeOpt/two_opt` and `routeOpt/or_opt` **stay where they are** — ALNS calls them via `#include`. Do NOT move them.

New dir: `core/alns/`

---

## Task 1 — Solution types (BLOCKER for everything)

**Files**: `core/alns/solution.h`

```cpp
namespace hfvrptwb::alns {

struct VehicleTrips {
    int vehicle_index;
    std::vector<RouteState> trips; // one RouteState per trip
};

struct ALNSSolution {
    std::vector<VehicleTrips> vehicles;
    std::vector<int> unrouted;  // 1-indexed node indices
    double objective;           // sum(trip.cost) + 1000 * unrouted.size()
};

double computeObjective(const std::vector<VehicleTrips>& vehicles, int unrouted_count);

// ConstructionResult → ALNSSolution (re-evaluates each trip via evaluateRouteState)
ALNSSolution fromConstruction(const ConstructionResult&, const solver::SolveRequest&,
                               double fixed, double km);

// ALNSSolution → ConstructionResult (flatten trips back)
ConstructionResult toConstruction(const ALNSSolution&);

} // namespace hfvrptwb::alns
```

**Key impl notes**:
- `fromConstruction`: for each route, if `route.trips` empty → wrap `route.nodes` in single trip; call `evaluateRouteState` to rebuild `RouteState` per trip
- `toConstruction`: flatten `VehicleTrips.trips[i].nodes` → `ConstructedRoute.trips[i]`; sum costs
- Drops in `ConstructionResult` → look up by `node.id()` → push to `unrouted`
- `computeObjective` = Σ trip.cost + 1000 * unrouted_count

---

## Task 2 — Destroy operators

**Files**: `core/alns/destroy.h`, `core/alns/destroy.cpp`

All signatures: `void xyzDestroy(ALNSSolution&, std::mt19937&, const solver::SolveRequest&, double fixed, double km, int q=4)`

**Helper needed first**:
```cpp
// Remove node_index from sol; add to unrouted; re-eval trip
void removeNode(ALNSSolution& sol, int vi, int ti, int node_index,
                const solver::SolveRequest& req, double fixed, double km);
// If trip becomes empty → erase trip; if vehicle has no trips → erase vehicle
```

**Operators** (implement in this order):

| # | Name | Logic |
|---|------|-------|
| 1 | `randomRemoval` | collect all (vi,ti,pos) tuples; shuffle; remove first q |
| 2 | `worstRemoval` | for each node: delta = cost_with - cost_without; remove q highest delta |
| 3 | `shawRemoval` | pick seed node; score others by `dist + |TWend_diff| + |demand_diff|`; remove q most similar |
| 4 | `priorityAwareRemoval` | sort unrouted candidates by priority ASC → remove q lowest |
| 5 | `routeConsolidationDestroy` | find 2 vehicles with lowest total_nodes; remove all their nodes to unrouted |
| 6 | `tripRemoval` | find trip with fewest nodes; remove all its nodes to unrouted |
| 7 | `tagViolationRemoval` | use `feasibility::isTagCompatible`; remove nodes in wrong vehicle |
| 8 | `lateCustomerRemoval` | need arrival_min from label; skip if no label; remove q with worst lateness |

**For worstRemoval cost_without**: call `evaluateRouteState` with node removed from nodes list.

---

## Task 3 — Repair operators

**Files**: `core/alns/repair.h`, `core/alns/repair.cpp`

All signatures: `void xyzRepair(ALNSSolution&, const solver::SolveRequest&, double fixed, double km, int reload_min)`

**Core insertion logic** (reuse from adaptive_constructor):
- Try insert node in existing trip: `evaluateInsertion(req, vehicle, trip, node_index, pos, fixed, km)`
- Try new trip in vehicle: `evaluateInsertion` on empty `RouteState`, then `validateTrips`
- Pick best by `delta_cost - priorityBonus(node)`

**Operators**:

| # | Name | Logic |
|---|------|-------|
| 1 | `greedyRepair` | for each unrouted (priority order): find best (vi,ti,pos); insert; if none → leave unrouted |
| 2 | `priorityFirstRepair` | same as greedy but sort unrouted by `priority DESC` before loop |
| 3 | `regret2Repair` | for each unrouted: find best + 2nd best cost; insert node with max `(2nd-best - best)` first |
| 4 | `proactiveBreakInsertion` | scan each trip for cumulative drive > 210 min (threshold = max_continuous - 30); insert break node after worst position |

**regret2 tip**: need `struct InsertionOption { int vi; int ti; int pos; double cost; }`. For each unrouted node collect top-2; regret = options[1].cost - options[0].cost; insert highest regret first.

**proactiveBreakInsertion**: `BreakNode` is a virtual stop — check if proto `Node` has a break type field, or track break positions separately in `VehicleTrips`.

---

## Task 4 — Local search (new operators only)

**Files**: `core/alns/local_search.h`, `core/alns/local_search.cpp`

**Existing (don't touch, just call)**:
- `twoOptTrip(req, vehicle, route_state, fixed, km)` → `routeOpt/two_opt.h`
- `orOptRelocate(plan, req, cfg)` → `routeOpt/or_opt.h` — takes `ConstructionResult&`

**How ALNS calls them**: after each repair, run local search via:
```cpp
// inside ALNS iteration:
applyTwoOptToSolution(candidate, req, cfg);  // call twoOptTrip per trip
// OR: convert → ConstructionResult → orOptRelocate → convert back (slower but correct)
```

**New operators**:

| # | Name | Logic |
|---|------|-------|
| 1 | `tripMerge` | for each vehicle: try merging trip i + trip j → 1 trip; feasible + cheaper → keep; run twoOptTrip on merged |
| 2 | `customerMoveAcrossTrips` | move single node from trip i to trip j (same vehicle); accept if total cost decreases |
| 3 | `twoOptStar` | swap route suffixes between vehicle A and B; accept if feasible + cheaper |
| 4 | `relocateAcrossVehicles` | move single node from vehicle A to vehicle B; accept if cheaper |

Skip `swapStar` and `vehicleTypeSwap` for now (complex, low priority).

---

## Task 5 — AdaptivePenalty

**File**: `core/alns/adaptive_penalty.h` (header-only, ~50 lines)

```cpp
class AdaptivePenalty {
    double cap_pen_ = 10.0, tw_pen_ = 10.0, overtime_pen_ = 5.0;
    static constexpr double TARGET = 0.20;
public:
    void update(double feasible_ratio) {
        double factor = (feasible_ratio < TARGET - 0.05) ? 1.2 : 
                        (feasible_ratio > TARGET + 0.05) ? 0.85 : 1.0;
        cap_pen_ = std::clamp(cap_pen_ * factor, 1.0, 1e6);
        tw_pen_  = std::clamp(tw_pen_  * factor, 1.0, 1e6);
        overtime_pen_ = std::clamp(overtime_pen_ * factor, 1.0, 1e6);
    }
    double capacity()  const { return cap_pen_; }
    double timeWindow() const { return tw_pen_; }
    double overtime()  const { return overtime_pen_; }
};
```

---

## Task 6 — ALNS framework

**Files**: `core/alns/alns.h`, `core/alns/alns.cpp`

```cpp
struct ALNSConfig {
    int    segment_size    = 100;
    double reaction_factor = 0.1;
    double initial_temp    = 100.0;
    double cooling_rate    = 0.9995;
    int    score_best      = 33;
    int    score_better    = 9;
    int    score_accepted  = 13;
};

class ALNSSolver {
public:
    ConstructionResult solve(
        const solver::SolveRequest& req,
        const ConstructionResult& initial,
        double fixed, double km,
        std::chrono::milliseconds budget,
        int reload_min = 0,
        uint32_t seed = 42);
};
```

**SA loop skeleton**:
```
current = fromConstruction(initial)
best = current

destroy_weights[8] = {1,1,1,1,1,1,1,1}
repair_weights[4]  = {1,1,1,1}

T = initial_temp
seg_feasible = 0, seg_total = 0

while time_remaining:
    d = pick weighted random destroy
    r = pick weighted random repair

    candidate = current
    d(candidate, ...)   // destroy
    r(candidate, ...)   // repair

    Δ = candidate.objective - current.objective
    accept = Δ < 0 || rand() < exp(-Δ / T)

    if accept:
        current = candidate
        seg_feasible++ if candidate.unrouted.empty()
    
    if candidate.objective < best.objective:
        best = candidate
        score = score_best
    elif candidate.objective < current.objective:
        score = score_better
    else:
        score = score_accepted
    
    // update weights each segment
    if iter % segment_size == 0:
        destroy_weights[d] = (1-r)*destroy_weights[d] + r*score
        repair_weights[r]  = (1-r)*repair_weights[r]  + r*score
        penalty.update(seg_feasible / seg_total)
        seg_feasible = seg_total = 0

    T *= cooling_rate

return toConstruction(best)
```

---

## Task 7 — Integration

### solver_service.cpp — current structure

```
solveRequest():
  t0 = now
  adaptiveConstruct(req, fixed, km, enableMultiTrip, reloadMin)  ← t1
  applyTwoOpt(plan, req, cfg)                                     ← t2
  orOptRelocate(plan, req, cfg)                                   ← t3
  writeResponse(...)
  // logs construction/2-opt/or-opt/total ms
```

### What to change

Replace the `applyTwoOpt` + `orOptRelocate` block with ALNS branch:

```cpp
if (cfg_.enableALNS) {
    // budget = remaining after construction
    int limit_ms = req->time_limit_ms() > 0 ? req->time_limit_ms() : 5000;
    int construction_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
    int alns_budget_ms = std::max(100, limit_ms - construction_ms);

    alns::ALNSSolver alns_solver;
    plan = alns_solver.solve(
        *req, plan,
        cfg_.fixedCostPerVehicle, cfg_.costPerKm,
        std::chrono::milliseconds(alns_budget_ms),
        cfg_.reloadMin, cfg_.seed);
    // ALNS does twoOpt + orOpt internally — skip standalone calls
} else {
    applyTwoOpt(plan, *req, cfg_);
    orOptRelocate(plan, *req, cfg_);
}
```

> `cfg_.seed` already in `SolveConfig` (type `uint32_t`). `req->time_limit_ms()` is in the proto.

### Add #include at top of solver_service.cpp
```cpp
#include "alns/alns.h"
```

**Modify**: `core/CMakeLists.txt`

Add to `FEASIBILITY_SRCS`:
```cmake
alns/destroy.cpp
alns/repair.cpp
alns/local_search.cpp
alns/alns.cpp
```

Add test executable:
```cmake
add_executable(alns_test
    alns/alns_test.cpp
    solver_service.cpp
    ${FEASIBILITY_SRCS}
    ${PROTO_SRCS} ${GRPC_SRCS}
)
target_link_libraries(alns_test PRIVATE gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(alns_test PRIVATE ${CMAKE_CURRENT_BINARY_DIR} ${CMAKE_CURRENT_SOURCE_DIR})
```

---

## Task 8 — Tests (TDD: write before impl)

**File**: `core/alns/alns_test.cpp`

Write these tests FIRST, then implement to make them pass:

```
TEST: fromConstruction round-trip
  - build SolveRequest with 3 nodes, 1 vehicle
  - adaptiveConstruct → fromConstruction → toConstruction
  - assert routes same, unrouted same

TEST: randomRemoval removes exactly q nodes
  - solution with 10 nodes in 2 vehicles
  - randomRemoval(sol, rng, req, fixed, km, q=3)
  - assert unrouted.size() == 3

TEST: greedyRepair re-inserts all unrouted
  - solution with 5 nodes, 3 unrouted
  - greedyRepair(sol, req, fixed, km, 0)
  - assert unrouted.empty() (feasible instance)

TEST: ALNS improves or equals construction objective
  - 20-node, 3-vehicle instance
  - run ALNSSolver with 500ms budget
  - assert alns_objective <= construction_objective

TEST: deterministic with fixed seed
  - run ALNSSolver twice with seed=42
  - assert objectives equal
```

Use `bench_gen.h` `Scenario` to generate test instances.

---

## Order of implementation

```
Task 8 (tests, red) → Task 1 → Task 5 → Task 2 → Task 3 → Task 4 → Task 6 → Task 7 → Task 8 (green)
```

Build cmd (MSYS2):
```bash
cd core
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target alns_test solver_regression_test
./build/alns_test
./build/solver_regression_test
```
