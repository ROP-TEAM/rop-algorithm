#include "construction/adaptive_constructor.h"

#include "validator/route_validator.h"
#include <algorithm>
#include <chrono>
#include <climits>
#include <cmath>
#include <limits>
#include <unordered_set>

namespace hfvrptwb {
namespace {

struct Candidate {
    int vehicle_index = -1;
    int insert_pos = -1;
    double score = std::numeric_limits<double>::infinity();
    double route_cost = 0.0;
    std::string fail_code;
    std::string fail_detail;
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

Candidate bestSingleInsertion(
    const solver::SolveRequest& req,
    int node_index,
    const std::vector<ConstructedRoute>& routes,
    double default_fixed_cost,
    double default_cost_per_km)
{
    Candidate best;
    const auto& node = req.nodes(node_index - 1);

    for (int vi = 0; vi < req.vehicles_size(); ++vi) {
        const auto& current = routes[vi];
        for (int pos = 0; pos <= (int)current.nodes.size(); ++pos) {
            auto candidate_nodes = inserted(current.nodes, node_index, pos);
            auto validation = validateTrips(
                req, req.vehicles(vi), {candidate_nodes}, default_fixed_cost, default_cost_per_km);
            if (!validation.feasible) {
                best.fail_code = validation.code;
                best.fail_detail = validation.detail;
                continue;
            }

            double delta = validation.total_cost - current.total_cost;
            double score = delta - priorityBonus(node);
            if (score < best.score) {
                best.vehicle_index = vi;
                best.insert_pos = pos;
                best.score = score;
                best.route_cost = validation.total_cost;
            }
        }
    }

    return best;
}

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
    const std::vector<ConstructedRoute>& routes,
    double default_fixed_cost,
    double default_cost_per_km)
{
    Candidate best;
    const auto& first = req.nodes(first_index - 1);

    for (int vi = 0; vi < req.vehicles_size(); ++vi) {
        const auto& current = routes[vi];
        for (int p1 = 0; p1 <= (int)current.nodes.size(); ++p1) {
            auto with_first = inserted(current.nodes, first_index, p1);
            for (int p2 = p1 + 1; p2 <= (int)with_first.size(); ++p2) {
                auto candidate_nodes = inserted(with_first, second_index, p2);
                auto validation = validateTrips(
                    req, req.vehicles(vi), {candidate_nodes}, default_fixed_cost, default_cost_per_km);
                if (!validation.feasible) {
                    best.fail_code = validation.code;
                    best.fail_detail = validation.detail;
                    continue;
                }

                double delta = validation.total_cost - current.total_cost;
                double score = delta - priorityBonus(first) - priorityBonus(req.nodes(second_index - 1));
                if (score < best.score) {
                    best.vehicle_index = vi;
                    best.insert_pos = p1;
                    best.score = score;
                    best.route_cost = validation.total_cost;
                }
            }
        }
    }

    return best;
}

void applyPairInsertion(
    ConstructedRoute& route,
    int pickup_index,
    int delivery_index,
    double route_cost,
    const solver::SolveRequest& req,
    double default_fixed_cost,
    double default_cost_per_km)
{
    double best_score = std::numeric_limits<double>::infinity();
    std::vector<int> best_nodes;
    for (int p1 = 0; p1 <= (int)route.nodes.size(); ++p1) {
        auto with_pickup = inserted(route.nodes, pickup_index, p1);
        for (int p2 = p1 + 1; p2 <= (int)with_pickup.size(); ++p2) {
            auto candidate_nodes = inserted(with_pickup, delivery_index, p2);
            auto validation = validateTrips(
                req, req.vehicles(route.vehicle_index), {candidate_nodes},
                default_fixed_cost, default_cost_per_km);
            if (validation.feasible && std::abs(validation.total_cost - route_cost) < 1e-6) {
                route.nodes = candidate_nodes;
                route.total_cost = route_cost;
                return;
            }
            if (validation.feasible && validation.total_cost < best_score) {
                best_score = validation.total_cost;
                best_nodes = std::move(candidate_nodes);
            }
        }
    }
    if (!best_nodes.empty()) {
        route.nodes = std::move(best_nodes);
        route.total_cost = best_score;
    }
}

} // namespace

ConstructionResult adaptiveConstruct(
    const solver::SolveRequest& req,
    double default_fixed_cost,
    double default_cost_per_km)
{
    ConstructionResult result;
    std::vector<ConstructedRoute> routes(req.vehicles_size());
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
                                          default_fixed_cost, default_cost_per_km);
            if (best.vehicle_index >= 0) {
                applyPairInsertion(routes[best.vehicle_index], pickup, delivery, best.route_cost,
                                   req, default_fixed_cost, default_cost_per_km);
                assigned.insert(pickup);
                assigned.insert(delivery);
            } else {
                result.drops.push_back({
                    node.id(),
                    best.fail_code.empty() ? "NO_FEASIBLE_INSERTION" : best.fail_code,
                    best.fail_detail.empty() ? "paired pickup/delivery could not be inserted" : best.fail_detail,
                });
            }
            continue;
        }

        auto best = bestSingleInsertion(req, node_index, routes,
                                        default_fixed_cost, default_cost_per_km);
        if (best.vehicle_index >= 0) {
            auto& route = routes[best.vehicle_index];
            route.nodes.insert(route.nodes.begin() + best.insert_pos, node_index);
            route.total_cost = best.route_cost;
            assigned.insert(node_index);
        } else {
            result.drops.push_back({
                node.id(),
                best.fail_code.empty() ? "NO_FEASIBLE_INSERTION" : best.fail_code,
                best.fail_detail.empty() ? "no vehicle/position passed validator" : best.fail_detail,
            });
        }
    }

    for (auto& route : routes) {
        if (!route.nodes.empty()) result.routes.push_back(std::move(route));
    }
    return result;
}

} // namespace hfvrptwb
