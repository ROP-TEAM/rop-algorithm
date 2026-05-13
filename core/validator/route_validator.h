#pragma once

#include "solver.pb.h"
#include <string>
#include <vector>

namespace hfvrptwb {

struct StopTiming {
    int node_index = 0;
    int arrival_min = 0;
    int depart_min = 0;
};

struct RouteValidationResult {
    bool feasible = true;
    std::string code;
    std::string detail;
    double total_distance_m = 0.0;
    int total_duration_min = 0;
    double total_cost = 0.0;
    std::vector<StopTiming> timings;
};

RouteValidationResult validateTrips(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const std::vector<std::vector<int>>& trips,
    double default_fixed_cost,
    double default_cost_per_km,
    int reload_min = 0);

} // namespace hfvrptwb
