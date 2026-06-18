#pragma once

#include "construction/adaptive_constructor.h"
#include "routing.pb.h"
#include "solver_service.h"

namespace hfvrptwb {

// Single-stop relocation across vehicles and trips (Or-opt-1).
// Moves one stop at a time to any feasible position in any route.
// Accepts first improvement and restarts until no move reduces cost.
void orOptRelocate(
    ConstructionResult& plan,
    const solver::SolveRequest& req,
    const SolveConfig& cfg);

} // namespace hfvrptwb
