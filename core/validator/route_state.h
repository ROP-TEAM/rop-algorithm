// validator/route_state.h
#pragma once
#include "validator/labels.h"
#include "solver.pb.h"
#include <string>
#include <vector>

namespace hfvrptwb {

struct RouteState {
    int vehicle_index = -1;
    std::vector<int> nodes;
    double distance_m = 0.0;
    int duration_min = 0;
    double cost = 0.0;
    double internal_score = 0.0;
    double total_wait_time = 0.0;
    std::vector<ForwardLabel> forward_labels;
    RouteLabelSummary label_summary;
};

struct InsertionEval {
    bool feasible = false;
    int position = -1;
    double delta_cost = 0.0;
    RouteState next;
    std::string fail_code;
    std::string fail_detail;
};

InsertionEval evaluateInsertion(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const RouteState& route,
    int node_index,
    int position,
    double default_fixed_cost,
    double default_cost_per_km,
    double weight_wait_time = 0.0);          // <-- default 0.0

InsertionEval evaluateRouteState(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const RouteState& current,
    std::vector<int> candidate_nodes,
    double default_fixed_cost,
    double default_cost_per_km,
    double weight_wait_time = 0.0);          // <-- default 0.0

} // namespace hfvrptwb
