#include "validator/route_validator.h"
#include <../config_manager.h>

#include <sstream>
#include <unordered_map>
#include <unordered_set>

namespace hfvrptwb {
namespace {

RouteValidationResult fail(std::string code, std::string detail) {
    RouteValidationResult r;
    r.feasible = false;
    r.code = std::move(code);
    r.detail = std::move(detail);
    return r;
}

double distAt(const solver::SolveRequest& req, int from, int to) {
    return req.distances(from * req.matrix_size() + to);
}

double durAt(const solver::SolveRequest& req, int from, int to) {
    return req.durations(from * req.matrix_size() + to);
}

bool tagCompatible(const solver::Vehicle& vehicle, const solver::Node& node) {
    if (vehicle.tags_size() == 0 || node.tags_size() == 0) return true;
    std::unordered_set<std::string> vehicle_tags(vehicle.tags().begin(), vehicle.tags().end());
    for (const auto& tag : node.tags()) {
        if (vehicle_tags.count(tag) != 0) return true;
    }
    return false;
}

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

std::string nodeName(const solver::SolveRequest& req, int node_index) {
    if (node_index <= 0 || node_index > req.nodes_size()) return "depot";
    return req.nodes(node_index - 1).id();
}

RouteValidationResult validateTripPrecedence(
    const solver::SolveRequest& req,
    const std::vector<int>& trip)
{
    bool seen_unpaired_pickup = false;
    std::unordered_set<std::string> seen_pickup_pairs;
    std::unordered_map<std::string, bool> pair_has_pickup;
    std::unordered_map<std::string, bool> pair_has_delivery;

    for (int idx : trip) {
        const auto& node = req.nodes(idx - 1);
        if (node.type() == "pickup") {
            if (!node.pair_id().empty()) {
                seen_pickup_pairs.insert(node.pair_id());
                pair_has_pickup[node.pair_id()] = true;
            } else {
                seen_unpaired_pickup = true;
            }
        } else if (node.type() == "delivery") {
            if (node.pair_id().empty() && seen_unpaired_pickup) {
                return fail("LB_PRECEDENCE", "delivery after pickup: " + node.id());
            }
            if (!node.pair_id().empty()) {
                pair_has_delivery[node.pair_id()] = true;
                if (seen_pickup_pairs.count(node.pair_id()) == 0) {
                    return fail("PD_PRECEDENCE", "delivery before pickup pair: " + node.pair_id());
                }
            }
        }
    }

    for (const auto& [pair_id, has_pickup] : pair_has_pickup) {
        if (has_pickup && pair_has_delivery[pair_id] == false) {
            return fail("PD_PAIR", "pickup without delivery pair: " + pair_id);
        }
    }
    for (const auto& [pair_id, has_delivery] : pair_has_delivery) {
        if (has_delivery && pair_has_pickup[pair_id] == false) {
            return fail("PD_PAIR", "delivery without pickup pair: " + pair_id);
        }
    }

    return {};
}

} // namespace

RouteValidationResult validateTrips(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const std::vector<std::vector<int>>& trips,
    double default_fixed_cost,
    double default_cost_per_km,
    double score_fixed_cost,
    double score_cost_per_km,
    int reload_min,
    double weight_wait_time)
{
    if (req.matrix_size() <= 0) return fail("MATRIX", "matrix_size must be positive");
    if (req.distances_size() != req.matrix_size() * req.matrix_size()) {
        return fail("MATRIX", "distance matrix length mismatch");
    }
    if (req.durations_size() != req.matrix_size() * req.matrix_size()) {
        return fail("MATRIX", "duration matrix length mismatch");
    }

    RouteValidationResult out;
    int time = vehicle.shift_start();
    const int shift_end = vehicle.shift_end() == 0 ? 1440 : vehicle.shift_end();
    bool break_taken = vehicle.break_start() == 0;

    double total_wait = 0.0;

    for (size_t trip_index = 0; trip_index < trips.size(); ++trip_index) {
        const auto& trip = trips[trip_index];
        if (vehicle.max_tasks() > 0 && (int)trip.size() > vehicle.max_tasks()) {
            return fail("MAX_TASKS", "trip stop count exceeds max_tasks");
        }

        auto prec = validateTripPrecedence(req, trip);
        if (!prec.feasible) return prec;

        int linehaul_load = 0;
        for (int idx : trip) {
            if (idx <= 0 || idx > req.nodes_size()) return fail("NODE_INDEX", "node index out of range");
            linehaul_load += linehaulDemand(req.nodes(idx - 1));
        }
        if (linehaul_load > vehicle.capacity()) {
            return fail("CAPACITY", "initial linehaul load exceeds vehicle capacity");
        }

        int onboard = linehaul_load;
        int cur = 0;
        for (int idx : trip) {
            const auto& node = req.nodes(idx - 1);
            if (!tagCompatible(vehicle, node)) {
                return fail("TAG", "vehicle " + vehicle.id() + " cannot serve node " + node.id());
            }

            int travel = (int)durAt(req, cur, idx);
            int arrival = time + travel;
            if (!break_taken && arrival >= vehicle.break_start()) {
                time = std::max(time, vehicle.break_end());
                break_taken = true;
                arrival = time + travel;
            }
            if (node.tw_start() > 0 && arrival < node.tw_start()) arrival = node.tw_start();

            if (node.tw_start() > 0 && arrival < node.tw_start()) {
              total_wait += (node.tw_start() - arrival);
              arrival = node.tw_start(); // arrival at tw_start
            }

            const int latest = node.tw_end() > 0 ? node.tw_end() : 1440;
            if (arrival > latest) {
                std::ostringstream msg;
                msg << node.id() << " arrival " << arrival << " > tw_end " << latest;
                return fail("TIME_WINDOW", msg.str());
            }

            onboard -= linehaulDemand(node);
            onboard += backhaulDemand(node);
            if (onboard > vehicle.capacity()) {
                return fail("CAPACITY", "load exceeds capacity at node " + node.id());
            }
            if (onboard < 0) {
                return fail("CAPACITY", "negative load at node " + node.id());
            }

            const int depart = arrival + node.service_time();
            out.timings.push_back({idx, arrival, depart});
            out.total_distance_m += distAt(req, cur, idx);
            time = depart;
            cur = idx;
        }

        out.total_distance_m += distAt(req, cur, 0);
        time += (int)durAt(req, cur, 0);
        if (trip_index + 1 < trips.size()) {
            time += reload_min;
        }
    }

    if (!break_taken && !trips.empty()) {
        return fail("BREAK", "required break was not taken");
    }
    if (time > shift_end) {
        std::ostringstream msg;
        msg << "route returns " << time << " > shift_end " << shift_end;
        return fail("SHIFT", msg.str());
    }
    if (vehicle.max_distance() > 0.0 && out.total_distance_m > vehicle.max_distance()) {
        return fail("MAX_DISTANCE", "route distance exceeds max_distance");
    }

    out.total_duration_min = time - vehicle.shift_start();
    const double fixed = vehicle.fixed_cost() > 0.0 ? vehicle.fixed_cost() : default_fixed_cost;
    const double per_km = vehicle.cost_per_km() > 0.0 ? vehicle.cost_per_km() : default_cost_per_km;
    out.total_cost = fixed + (out.total_distance_m / 1000.0) * per_km;

    double wait_time_penalty_rate = weight_wait_time;
    out.total_wait_time = total_wait;
    // internal score — used by solver only
    out.internal_score = score_fixed_cost
                       + (out.total_distance_m / 1000.0) * score_cost_per_km
                       + (total_wait * weight_wait_time);

    return out;
}

} // namespace hfvrptwb
