#pragma once

#include "construction/adaptive_constructor.h"
#include "validator/route_state.h"
#include "validator/route_validator.h"
#include <unordered_set>
#include <vector>
#include <cmath>

namespace hfvrptwb {
namespace alns {

struct VehicleTrips {
    int vehicle_index = -1;
    std::vector<RouteState> trips;
    double internal_score = 0.0;
};

struct ALNSSolution {
    std::vector<VehicleTrips> vehicles;
    std::vector<int> unrouted;
    double objective = 0.0;
    bool forbid_new_vehicle = false;
    double internal_score = 0.0;
};

inline double computeObjective(const std::vector<VehicleTrips>& vehicles, int unrouted_count,
                                double penalty_coeff = 5000.0) {
    double total = 0.0;
    for (const auto& vt : vehicles) {
        for (const auto& trip : vt.trips) {
            total += trip.cost;
        }
    }
    total += penalty_coeff * unrouted_count;
    return total;
}

inline double computeInternalScore(const std::vector<VehicleTrips>& vehicles,
                                   int unrouted_count,
                                   double penalty_coeff = 1000.0) {
    double total = 0.0;
    for (const auto& vt : vehicles) {
        for (const auto& trip : vt.trips) {
            total += trip.cost;  
        }
    }
    total += penalty_coeff * unrouted_count;
    return total;
}

inline ALNSSolution fromConstruction(
    const ConstructionResult& result,
    const solver::SolveRequest& req,
    double fixed, double km,
    double weight_wait_time = 0.0,
    int reload_min = 0)  // ← add reload_min
{
    ALNSSolution sol;
    std::unordered_set<int> routed;

    for (const auto& route : result.routes) {
        if (route.vehicle_index < 0 || route.vehicle_index >= req.vehicles_size())
            continue;

        const auto& vehicle = req.vehicles(route.vehicle_index);
        VehicleTrips vt;
        vt.vehicle_index = route.vehicle_index;

        std::vector<std::vector<int>> trip_lists = route.trips.empty()
            ? std::vector<std::vector<int>>{route.nodes}
            : route.trips;

        // ← validate full multi-trip schedule with correct reload_min first
        // this prevents silently dropping orders from feasible multi-trip routes
        if (trip_lists.size() > 1) {
            auto full_val = validateTrips(req, vehicle, trip_lists,
                                          fixed, km, fixed, km,
                                          reload_min, weight_wait_time);
            if (!full_val.feasible) continue;  // skip whole route
        }

        for (const auto& trip_nodes : trip_lists) {
            if (trip_nodes.empty()) continue;

            RouteState init;
            init.vehicle_index = route.vehicle_index;

            auto eval = evaluateRouteState(req, vehicle, init, trip_nodes,
                                           fixed, km);
            if (eval.feasible && std::isfinite(eval.next.cost)) {
                vt.trips.push_back(std::move(eval.next));
                for (int ni : trip_nodes) routed.insert(ni);
            }
        }

        if (!vt.trips.empty()) {
            sol.vehicles.push_back(std::move(vt));
        }
    }

    for (int i = 0; i < req.nodes_size(); ++i) {
        int node_index = i + 1;
        if (routed.find(node_index) == routed.end()) {
            sol.unrouted.push_back(node_index);
        }
    }

    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size());
    sol.internal_score = computeInternalScore(sol.vehicles, (int)sol.unrouted.size());
    return sol;
}

inline ConstructionResult toConstruction(const ALNSSolution& sol) {
    ConstructionResult result;

    for (const auto& vt : sol.vehicles) {
        ConstructedRoute route;
        route.vehicle_index = vt.vehicle_index;
        route.total_cost = 0.0;
        route.internal_score = 0.0;

        for (const auto& trip : vt.trips) {
            route.trips.push_back(trip.nodes);
            route.total_cost += trip.cost;
            route.internal_score += trip.cost;
            route.nodes.insert(route.nodes.end(), trip.nodes.begin(), trip.nodes.end());
        }

        result.routes.push_back(std::move(route));
    }

    for (int ni : sol.unrouted) {
        ConstructionDrop drop;
        drop.node_id = std::to_string(ni);
        drop.code = "UNROUTED";
        result.drops.push_back(std::move(drop));
    }

    return result;
}

} // namespace alns
} // namespace hfvrptwb
