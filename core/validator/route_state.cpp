#include "validator/route_state.h"

#include "validator/route_validator.h"

namespace hfvrptwb {

InsertionEval evaluateRouteState(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const RouteState& current,
    std::vector<int> candidate_nodes,
    double default_fixed_cost,
    double default_cost_per_km)
{
    InsertionEval eval;
    eval.next.vehicle_index = current.vehicle_index;
    eval.next.nodes = std::move(candidate_nodes);

    auto validation = validateTrips(
        req, vehicle, {eval.next.nodes}, default_fixed_cost, default_cost_per_km);
    if (!validation.feasible) {
        eval.fail_code = validation.code;
        eval.fail_detail = validation.detail;
        return eval;
    }

    eval.feasible = true;
    eval.delta_cost = validation.total_cost - current.cost;
    eval.next.distance_m = validation.total_distance_m;
    eval.next.duration_min = validation.total_duration_min;
    eval.next.cost = validation.total_cost;
    return eval;
}

InsertionEval evaluateInsertion(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const RouteState& route,
    int node_index,
    int position,
    double default_fixed_cost,
    double default_cost_per_km)
{
    InsertionEval eval;
    if (position < 0 || position > (int)route.nodes.size()) {
        eval.fail_code = "INSERT_POSITION";
        eval.fail_detail = "insert position out of range";
        return eval;
    }

    auto candidate_nodes = route.nodes;
    candidate_nodes.insert(candidate_nodes.begin() + position, node_index);
    eval = evaluateRouteState(
        req, vehicle, route, std::move(candidate_nodes), default_fixed_cost, default_cost_per_km);
    eval.position = position;
    return eval;
}

} // namespace hfvrptwb
