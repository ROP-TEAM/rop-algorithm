#include "validator/route_state.h"
#include "validator/route_validator.h"

namespace hfvrptwb {
namespace {

int linehaulDemand(const solver::Node& node) {
    if (node.linehaul_demand() != 0 || node.backhaul_demand() != 0) {
        return node.linehaul_demand();
    }
    return node.type() == "pickup" ? 0 : node.demand();
}

int backhaulDemand(const solver::Node& node) {
    if (node.linehaul_demand() != 0 || node.backhaul_demand() != 0) {
        return node.backhaul_demand();
    }
    return node.type() == "pickup" ? node.demand() : 0;
}

int travelAt(const solver::SolveRequest& req, int from, int to) {
    return (int)req.durations(from * req.matrix_size() + to);
}

double distanceAt(const solver::SolveRequest& req, int from, int to) {
    return req.distances(from * req.matrix_size() + to);
}

std::vector<ForwardLabel> buildForwardLabels(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const std::vector<int>& nodes)
{
    std::vector<ForwardLabel> labels;
    labels.reserve(nodes.size());

    ForwardLabel label;
    label.earliest_arrival = vehicle.shift_start();
    label.latest_arrival = vehicle.shift_end() == 0 ? 1440 : vehicle.shift_end();
    label.departure_time = vehicle.shift_start();
    for (int idx : nodes) {
        label.load_linehaul += linehaulDemand(req.nodes(idx - 1));
    }
    for (int idx : nodes) {
        const auto& node = req.nodes(idx - 1);
        StopSpec stop{
            node.tw_start(),
            node.tw_end() > 0 ? node.tw_end() : 1440,
            node.service_time(),
            linehaulDemand(node),
            backhaulDemand(node),
        };
        int from = labels.empty() ? 0 : nodes[labels.size() - 1];
        label = extendForwardLabel(
            label, stop, travelAt(req, from, idx), distanceAt(req, from, idx));
        labels.push_back(label);
    }

    return labels;
}

} // namespace

InsertionEval evaluateRouteState(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const RouteState& current,
    std::vector<int> candidate_nodes,
    double default_fixed_cost,
    double default_cost_per_km,
    double weight_wait_time)    // <-- keep parameter
{
    InsertionEval eval;
    eval.next.vehicle_index = current.vehicle_index;
    eval.next.nodes = std::move(candidate_nodes);

    auto validation = validateTrips(
        req, vehicle, {eval.next.nodes},
        550.0, 4.0003,                              // billing
        default_fixed_cost, default_cost_per_km,    // mode weights
        0,                                          // reload_min
        weight_wait_time);                          // <-- use it here

    if (!validation.feasible) {
        eval.fail_code = validation.code;
        eval.fail_detail = validation.detail;
        return eval;
    }

    eval.feasible = true;
    double current_cost = current.cost;
    eval.delta_cost = validation.total_cost - current_cost;
    eval.next.distance_m       = validation.total_distance_m;
    eval.next.duration_min     = validation.total_duration_min;
    eval.next.cost             = validation.total_cost;
    eval.next.total_wait_time  = validation.total_wait_time;
    eval.next.internal_score   = validation.internal_score;

    eval.next.forward_labels = buildForwardLabels(req, vehicle, eval.next.nodes);
    eval.next.label_summary = summarizeForwardLabels(
        eval.next.forward_labels,
        vehicle.capacity(),
        vehicle.shift_end() == 0 ? 1440 : vehicle.shift_end(),
        eval.next.distance_m);

    return eval;
}

InsertionEval evaluateInsertion(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const RouteState& route,
    int node_index,
    int position,
    double default_fixed_cost,
    double default_cost_per_km,
    double weight_wait_time)    // <-- keep parameter
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
        req, vehicle, route, std::move(candidate_nodes),
        default_fixed_cost, default_cost_per_km,
        weight_wait_time);          // forward
    eval.position = position;
    return eval;
}

} // namespace hfvrptwb
