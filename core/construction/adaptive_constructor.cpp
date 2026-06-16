#include "construction/adaptive_constructor.h"

#include "validator/route_state.h"
#include "validator/route_validator.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <limits>
#include <unordered_set>
#include <numeric>
#include <random>

namespace hfvrptwb {
namespace {

struct Candidate {
    int vehicle_index = -1;
    int trip_index = -1;      // -1 = new trip appended, -2 = new trip prepended
    int insert_pos = -1;
    double score = std::numeric_limits<double>::infinity();
    double route_cost = 0.0;
    InsertionEval eval;       // stored for later use in applyMultiTripInsertion
    std::string fail_code;
    std::string fail_detail;
};

struct ScheduleState {
    int vehicle_index = -1;
    std::vector<RouteState> trips;
    double cost = 0.0;
};

int sortDeadline(const solver::Node& n) {
    return n.deadline_min() == 0 ? INT_MAX : n.deadline_min();
}

std::vector<int> priorityOrder(const solver::SolveRequest& req) {
    std::vector<int> order(req.nodes_size());
    for (int i = 0; i < req.nodes_size(); ++i) order[i] = i + 1;
    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& na = req.nodes(a - 1);
        const auto& nb = req.nodes(b - 1);
        int da = sortDeadline(na);
        int db = sortDeadline(nb);
        if (da != db) return da < db;
        if (na.priority() != nb.priority()) return na.priority() > nb.priority();
        return na.id() < nb.id();
    });
    return order;
}

bool isRouteStateEmpty(const std::vector<RouteState>& states, int idx) {
    return states[idx].nodes.empty();
}
bool isScheduleStateEmpty(const std::vector<ScheduleState>& states, int idx) {
    return states[idx].trips.empty();
}

template <typename StateVec, typename IsEmptyFunc>
std::vector<int> getSmartVehicleOrder(
    const solver::SolveRequest& req,
    const StateVec& states,
    double default_fixed_cost,      // same name as in caller
    double default_cost_per_km,     // same name as in caller
    IsEmptyFunc isEmpty)
{
    std::vector<int> order(req.vehicles_size());
    std::iota(order.begin(), order.end(), 0);

    thread_local std::mt19937 rng(std::random_device{}());
    std::shuffle(order.begin(), order.end(), rng);

    std::stable_sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& va = req.vehicles(a);
        const auto& vb = req.vehicles(b);

        bool active_a = !isEmpty(states, a);
        bool active_b = !isEmpty(states, b);

        // Dynamic rule – adapt to mode weights
        if (active_a != active_b)
        return active_a < active_b;   // always spread orders during construction

        double cost_a = va.fixed_cost() > 0 ? va.fixed_cost() : default_fixed_cost;
        double cost_b = vb.fixed_cost() > 0 ? vb.fixed_cost() : default_fixed_cost;
        if (cost_a != cost_b) return cost_a < cost_b;

        if (va.capacity() != vb.capacity()) return va.capacity() > vb.capacity();

        return false;
    });
    return order;
}

bool expired(std::chrono::steady_clock::time_point start, int limit_ms) {
    if (limit_ms <= 0) return false;
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
    return elapsed.count() >= limit_ms;
}

double priorityBonus(const solver::Node& node) {
    double bonus = node.priority() * 10.0;
    if (node.deadline_min() > 0) bonus += 5.0;
    if (node.must_serve()) bonus += 100.0;
    return bonus;
}

std::vector<int> inserted(const std::vector<int>& route, int node_index, int pos) {
    std::vector<int> candidate = route;
    candidate.insert(candidate.begin() + pos, node_index);
    return candidate;
}

// ---------- Single‑trip insertion ----------
Candidate bestSingleInsertion(
    const solver::SolveRequest& req,
    int node_index,
    const std::vector<RouteState>& routes,
    double weight_fixed_cost,
    double weight_per_km)
{
    Candidate best;

    auto smart_order = getSmartVehicleOrder(req, routes, weight_fixed_cost, weight_per_km, isRouteStateEmpty);
    for (int vi : smart_order) {
        const auto& current = routes[vi];
        for (int pos = 0; pos <= (int)current.nodes.size(); ++pos) {
            auto eval = evaluateInsertion(
                req, req.vehicles(vi), current, node_index, pos,
                weight_fixed_cost, weight_per_km);
            if (!eval.feasible) {
                best.fail_code = eval.fail_code;
                best.fail_detail = eval.fail_detail;
                continue;
            }

            double score = eval.delta_cost - priorityBonus(req.nodes(node_index - 1));
            if (score < best.score) {
                best.vehicle_index = vi;
                best.insert_pos = pos;
                best.score = score;
                best.route_cost = eval.next.cost;
            }
        }
    }

    return best;
}

double scheduleCost(const std::vector<RouteState>& trips) {
    double cost = 0.0;
    for (const auto& trip : trips) cost += trip.cost;
    return cost;
}

std::vector<std::vector<int>> tripNodes(const std::vector<RouteState>& trips) {
    std::vector<std::vector<int>> result;
    result.reserve(trips.size());
    for (const auto& trip : trips) result.push_back(trip.nodes);
    return result;
}

std::vector<int> flattenTrips(const std::vector<RouteState>& trips) {
    std::vector<int> nodes;
    for (const auto& trip : trips) {
        nodes.insert(nodes.end(), trip.nodes.begin(), trip.nodes.end());
    }
    return nodes;
}

// ---------- Multi‑trip insertion ----------
Candidate bestMultiTripInsertion(
    const solver::SolveRequest& req,
    int node_index,
    const std::vector<ScheduleState>& schedules,
    double weight_fixed_cost,
    double weight_per_km,
    double billing_fixed_cost,
    double billing_cost_per_km,
    int reload_min)
{
    Candidate best;
    auto smart_order = getSmartVehicleOrder(req, schedules, billing_fixed_cost, billing_cost_per_km, isScheduleStateEmpty);
    for (int vi : smart_order) {
        const auto& schedule = schedules[vi];

        // 1) Try to insert into an existing trip
        for (int ti = 0; ti < (int)schedule.trips.size(); ++ti) {
            const auto& trip = schedule.trips[ti];
            for (int pos = 0; pos <= (int)trip.nodes.size(); ++pos) {
                auto eval = evaluateInsertion(
                    req, req.vehicles(vi), trip, node_index, pos,
                    weight_fixed_cost, weight_per_km);
                if (!eval.feasible) {
                    best.fail_code = eval.fail_code;
                    best.fail_detail = eval.fail_detail;
                    continue;
                }
                auto candidate_trips = schedule.trips;
                candidate_trips[ti] = eval.next;
                auto validation = validateTrips(
                    req, req.vehicles(vi), tripNodes(candidate_trips),
                    billing_fixed_cost, billing_cost_per_km,
                    weight_fixed_cost, weight_per_km,
                    reload_min, 0.0);
                if (!validation.feasible) {
                    best.fail_code = validation.code;
                    best.fail_detail = validation.detail;
                    continue;
                }
                double delta = eval.next.cost - trip.cost;
                double score = delta - priorityBonus(req.nodes(node_index - 1));
                if (score < best.score) {
                    best.vehicle_index = vi;
                    best.trip_index = ti;
                    best.insert_pos = pos;
                    best.score = score;
                    best.route_cost = eval.next.cost;
                    best.eval = std::move(eval);
                }
            }
        }

        // 2) Try a new trip **appended** at the end
        {
            RouteState new_trip;
            new_trip.vehicle_index = vi;
            auto eval = evaluateInsertion(
                req, req.vehicles(vi), new_trip, node_index, 0,
                weight_fixed_cost, weight_per_km);
            if (eval.feasible) {
                auto candidate_trips = schedule.trips;
                candidate_trips.push_back(eval.next);
                auto validation = validateTrips(
                    req, req.vehicles(vi), tripNodes(candidate_trips),
                    billing_fixed_cost, billing_cost_per_km,
                    weight_fixed_cost, weight_per_km,
                    reload_min, 0.0);
                if (validation.feasible) {
                    double delta = eval.next.cost;
                    double score = delta - priorityBonus(req.nodes(node_index - 1));
                    if (score < best.score) {
                        best.vehicle_index = vi;
                        best.trip_index = -1;   // appended
                        best.insert_pos = 0;
                        best.score = score;
                        best.route_cost = eval.next.cost;
                        best.eval = std::move(eval);
                    }
                } else {
                    best.fail_code = validation.code;
                    best.fail_detail = validation.detail;
                }
            } else {
                best.fail_code = eval.fail_code;
                best.fail_detail = eval.fail_detail;
            }
        }

        // 3) Try a new trip **prepended** before the first trip
        {
            RouteState new_trip;
            new_trip.vehicle_index = vi;
            auto eval = evaluateInsertion(
                req, req.vehicles(vi), new_trip, node_index, 0,
                weight_fixed_cost, weight_per_km);
            if (eval.feasible) {
                // Build candidate schedule with the new trip first
                std::vector<RouteState> candidate_trips2;
                candidate_trips2.reserve(schedule.trips.size() + 1);
                candidate_trips2.push_back(eval.next);
                candidate_trips2.insert(candidate_trips2.end(),
                                       schedule.trips.begin(), schedule.trips.end());
                auto validation = validateTrips(
                    req, req.vehicles(vi), tripNodes(candidate_trips2),
                    billing_fixed_cost, billing_cost_per_km,
                    weight_fixed_cost, weight_per_km,
                    reload_min, 0.0);
                if (validation.feasible) {
                    double delta = eval.next.cost;
                    double score = delta - priorityBonus(req.nodes(node_index - 1));
                    if (score < best.score) {
                        best.vehicle_index = vi;
                        best.trip_index = -2;   // prepended
                        best.insert_pos = 0;
                        best.score = score;
                        best.route_cost = eval.next.cost;
                        best.eval = std::move(eval);
                    }
                } else {
                    best.fail_code = validation.code;
                    best.fail_detail = validation.detail;
                }
            } else {
                best.fail_code = eval.fail_code;
                best.fail_detail = eval.fail_detail;
            }
        }
    }

    return best;
}

// ---------- Apply a multi‑trip insertion ----------
void applyMultiTripInsertion(
    ScheduleState& schedule,
    const solver::SolveRequest& req,
    int node_index,
    const Candidate& best,
    double weight_fixed_cost,
    double weight_per_km)
{
    const auto& vehicle = req.vehicles(schedule.vehicle_index);

    if (best.trip_index == -2) {
        // Prepend new trip
        RouteState new_trip;
        new_trip.vehicle_index = schedule.vehicle_index;
        auto eval = evaluateInsertion(
            req, vehicle, new_trip, node_index, 0,
            weight_fixed_cost, weight_per_km);
        if (eval.feasible)
            schedule.trips.insert(schedule.trips.begin(), std::move(eval.next));
    } else if (best.trip_index == -1 || best.trip_index == (int)schedule.trips.size()) {
        // Append new trip
        RouteState new_trip;
        new_trip.vehicle_index = schedule.vehicle_index;
        auto eval = evaluateInsertion(
            req, vehicle, new_trip, node_index, 0,
            weight_fixed_cost, weight_per_km);
        if (eval.feasible)
            schedule.trips.push_back(std::move(eval.next));
    } else {
        // Insert into existing trip
        auto& trip = schedule.trips[best.trip_index];
        auto eval = evaluateInsertion(
            req, vehicle, trip, node_index, best.insert_pos,
            weight_fixed_cost, weight_per_km);
        if (eval.feasible)
            trip = std::move(eval.next);
    }
    schedule.cost = scheduleCost(schedule.trips);
}

// ---------- Remaining helpers (unchanged) ----------
int findPair(const solver::SolveRequest& req, const solver::Node& node) {
    if (node.pair_id().empty()) return -1;
    const std::string want_type = node.type() == "pickup" ? "delivery" : "pickup";
    for (int i = 0; i < req.nodes_size(); ++i) {
        const auto& other = req.nodes(i);
        if (other.pair_id() == node.pair_id() && other.type() == want_type) return i + 1;
    }
    return -1;
}

Candidate bestPairInsertion(
    const solver::SolveRequest& req,
    int first_index,
    int second_index,
    const std::vector<RouteState>& routes,
    double weight_fixed_cost,
    double weight_per_km)
{
    Candidate best;
    const auto& first = req.nodes(first_index - 1);

    for (int vi = 0; vi < req.vehicles_size(); ++vi) {
        const auto& current = routes[vi];
        for (int p1 = 0; p1 <= (int)current.nodes.size(); ++p1) {
            auto with_first = inserted(current.nodes, first_index, p1);
            for (int p2 = p1 + 1; p2 <= (int)with_first.size(); ++p2) {
                auto candidate_nodes = inserted(with_first, second_index, p2);
                auto eval = evaluateRouteState(
                    req, req.vehicles(vi), current, std::move(candidate_nodes),
                    weight_fixed_cost, weight_per_km);
                if (!eval.feasible) {
                    best.fail_code = eval.fail_code;
                    best.fail_detail = eval.fail_detail;
                    continue;
                }

                double score = eval.delta_cost - priorityBonus(first) - priorityBonus(req.nodes(second_index - 1));
                if (score < best.score) {
                    best.vehicle_index = vi;
                    best.insert_pos = p1;
                    best.score = score;
                    best.route_cost = eval.next.cost;
                }
            }
        }
    }

    return best;
}

void applyPairInsertion(
    RouteState& route,
    int pickup_index,
    int delivery_index,
    double route_cost,
    const solver::SolveRequest& req,
    double weight_fixed_cost,
    double weight_per_km)
{
    double best_score = std::numeric_limits<double>::infinity();
    std::vector<int> best_nodes;
    for (int p1 = 0; p1 <= (int)route.nodes.size(); ++p1) {
        auto with_pickup = inserted(route.nodes, pickup_index, p1);
        for (int p2 = p1 + 1; p2 <= (int)with_pickup.size(); ++p2) {
            auto candidate_nodes = inserted(with_pickup, delivery_index, p2);
            auto eval = evaluateRouteState(
                req, req.vehicles(route.vehicle_index), route, std::move(candidate_nodes),
                weight_fixed_cost, weight_per_km);
            if (eval.feasible && std::abs(eval.next.cost - route_cost) < 1e-6) {
                route = std::move(eval.next);
                return;
            }
            if (eval.feasible && eval.next.cost < best_score) {
                best_score = eval.next.cost;
                best_nodes = std::move(eval.next.nodes);
            }
        }
    }
    if (!best_nodes.empty()) {
        route.nodes = std::move(best_nodes);
        route.cost = best_score;
    }
}

} // namespace

ConstructionResult adaptiveConstruct(
    const solver::SolveRequest& req,
    double weight_fixed_cost,
    double weight_per_km,
    double billing_fixed_cost,
    double billing_cost_per_km,
    bool enable_multi_trip,
    int reload_min)
{
    ConstructionResult result;

    if (enable_multi_trip) {
        std::vector<ScheduleState> schedules(req.vehicles_size());
        for (int vi = 0; vi < req.vehicles_size(); ++vi) schedules[vi].vehicle_index = vi;

        auto start = std::chrono::steady_clock::now();
        std::unordered_set<int> assigned;

        for (int node_index : priorityOrder(req)) {
            if (assigned.count(node_index) != 0) continue;
            if (expired(start, req.time_limit_ms())) {
                result.timed_out = true;
                break;
            }

            const auto& node = req.nodes(node_index - 1);
            if (findPair(req, node) > 0) {
                ConstructionDrop drop;
                drop.node_id = node.id();
                drop.code = "NO_FEASIBLE_INSERTION";
                drop.detail = "paired pickup/delivery multi-trip insertion is not enabled yet";
                result.drops.push_back(std::move(drop));
                continue;
            }

            auto best = bestMultiTripInsertion(
                req, node_index, schedules,
                weight_fixed_cost, weight_per_km,
                billing_fixed_cost, billing_cost_per_km,
                reload_min);
            if (best.vehicle_index >= 0) {
                applyMultiTripInsertion(
                    schedules[best.vehicle_index], req, node_index, best,
                    weight_fixed_cost, weight_per_km);
                assigned.insert(node_index);
            } else {
                ConstructionDrop drop;
                drop.node_id = node.id();
                drop.code = best.fail_code.empty() ? "NO_FEASIBLE_INSERTION" : best.fail_code;
                drop.detail = best.fail_detail.empty() ? "no vehicle/trip/position passed validator" : best.fail_detail;
                result.drops.push_back(std::move(drop));
            }
        }

        for (auto& schedule : schedules) {
            if (schedule.trips.empty()) continue;
            auto flat_nodes = flattenTrips(schedule.trips);
            std::vector<std::vector<int>> trips;
            trips.reserve(schedule.trips.size());
            for (auto& trip : schedule.trips) trips.push_back(std::move(trip.nodes));
            result.routes.push_back({
                schedule.vehicle_index,
                std::move(flat_nodes),
                std::move(trips),
                schedule.cost,
                schedule.cost,
            });
        }
        return result;
    }

    // Single‑trip path (unchanged)
    std::vector<RouteState> routes(req.vehicles_size());
    for (int vi = 0; vi < req.vehicles_size(); ++vi) routes[vi].vehicle_index = vi;

    auto start = std::chrono::steady_clock::now();
    std::unordered_set<int> assigned;

    for (int node_index : priorityOrder(req)) {
        if (assigned.count(node_index) != 0) continue;
        if (expired(start, req.time_limit_ms())) {
            result.timed_out = true;
            break;
        }

        const auto& node = req.nodes(node_index - 1);
        int pair_index = findPair(req, node);
        if (pair_index > 0) {
            if (assigned.count(pair_index) != 0) continue;
            int pickup = node.type() == "pickup" ? node_index : pair_index;
            int delivery = node.type() == "delivery" ? node_index : pair_index;
            auto best = bestPairInsertion(req, pickup, delivery, routes,
                                          weight_fixed_cost, weight_per_km);
            if (best.vehicle_index >= 0) {
                applyPairInsertion(routes[best.vehicle_index], pickup, delivery, best.route_cost,
                                   req, weight_fixed_cost, weight_per_km);
                assigned.insert(pickup);
                assigned.insert(delivery);
            } else {
                ConstructionDrop drop;
                drop.node_id = node.id();
                drop.code = best.fail_code.empty() ? "NO_FEASIBLE_INSERTION" : best.fail_code;
                drop.detail = best.fail_detail.empty() ? "paired pickup/delivery could not be inserted" : best.fail_detail;
                result.drops.push_back(std::move(drop));
            }
            continue;
        }

        auto best = bestSingleInsertion(req, node_index, routes,
                                        weight_fixed_cost, weight_per_km);
        if (best.vehicle_index >= 0) {
            auto& route = routes[best.vehicle_index];
            auto eval = evaluateInsertion(
                req, req.vehicles(best.vehicle_index), route, node_index, best.insert_pos,
                weight_fixed_cost, weight_per_km);
            route = std::move(eval.next);
            assigned.insert(node_index);
        } else {
            ConstructionDrop drop;
            drop.node_id = node.id();
            drop.code = best.fail_code.empty() ? "NO_FEASIBLE_INSERTION" : best.fail_code;
            drop.detail = best.fail_detail.empty() ? "no vehicle/position passed validator" : best.fail_detail;
            result.drops.push_back(std::move(drop));
        }
    }

    for (auto& route : routes) {
        if (!route.nodes.empty()) {
            result.routes.push_back({
                route.vehicle_index,
                std::move(route.nodes),
                {},
                route.cost,
                route.cost,
            });
        }
    }
    return result;
}

} // namespace hfvrptwb
