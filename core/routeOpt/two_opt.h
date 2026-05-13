#pragma once

#include "solver.pb.h"
#include "validator/route_state.h"

namespace hfvrptwb {

// Applies 2-opt improvement to a single trip in-place.
// Rejects any swap that violates TW constraints or increases cost.
// Returns improved RouteState (same nodes, better order).
RouteState twoOptTrip(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    RouteState trip,
    double default_fixed_cost,
    double default_cost_per_km);

} // namespace hfvrptwb
