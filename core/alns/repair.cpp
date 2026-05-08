#include "alns/repair.h"
#include "validator/route_state.h"
#include "validator/route_validator.h"
#include <algorithm>
#include <limits>

namespace hfvrptwb {
namespace alns {

namespace {

double priorityBonus(const solver::Node& node) {
    double bonus = node.priority() * 10.0;
    if (node.deadline_min() > 0) bonus += 5.0;
    if (node.must_serve()) bonus += 100.0;
    return bonus;
}

std::vector<int> sortUnrouted(const std::vector<int>& unrouted,
                               const solver::SolveRequest& req) {
    std::vector<int> order = unrouted;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        const auto& na = req.nodes(a - 1);
        const auto& nb = req.nodes(b - 1);
        int da = na.deadline_min() > 0 ? na.deadline_min() : std::numeric_limits<int>::max();
        int db = nb.deadline_min() > 0 ? nb.deadline_min() : std::numeric_limits<int>::max();
        if (da != db) return da < db;
        if (na.priority() != nb.priority()) return na.priority() > nb.priority();
        return a < b;
    });
    return order;
}

struct InsertionOption {
    int vi = -1;
    int ti = -1;
    int pos = -1;
    double cost = std::numeric_limits<double>::infinity();
    InsertionEval eval;
};

InsertionOption findBestInsertion(
    const ALNSSolution& sol, int node_index,
    const solver::SolveRequest& req, double fixed, double km, int reload_min)
{
    InsertionOption best;

    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        const auto& vt = sol.vehicles[vi];
        const auto& vehicle = req.vehicles(vt.vehicle_index);

        // Try existing trips
        for (int ti = 0; ti < (int)vt.trips.size(); ++ti) {
            const auto& trip = vt.trips[ti];
            for (int pos = 0; pos <= (int)trip.nodes.size(); ++pos) {
                auto eval = evaluateInsertion(req, vehicle, trip, node_index, pos, fixed, km);
                if (eval.feasible && std::isfinite(eval.delta_cost)) {
                    double score = eval.delta_cost - priorityBonus(req.nodes(node_index - 1));
                    if (score < best.cost) {
                        best.vi = vi;
                        best.ti = ti;
                        best.pos = pos;
                        best.cost = score;
                        best.eval = std::move(eval);
                    }
                }
            }
        }

        // Try new trip in this vehicle
        RouteState empty_route;
        empty_route.vehicle_index = vt.vehicle_index;
        auto eval = evaluateInsertion(req, vehicle, empty_route, node_index, 0, fixed, km);
        if (eval.feasible && std::isfinite(eval.next.cost)) {
            // Validate full multi-trip schedule including reload time
            std::vector<std::vector<int>> all_trips;
            double existing_cost = 0.0;
            for (const auto& t : vt.trips) {
                all_trips.push_back(t.nodes);
                existing_cost += t.cost;
            }
            all_trips.push_back(eval.next.nodes);

            auto full_val = validateTrips(req, vehicle, all_trips, fixed, km, reload_min);
            if (full_val.feasible && std::isfinite(full_val.total_cost)) {
                double score = full_val.total_cost - existing_cost
                             - priorityBonus(req.nodes(node_index - 1));
                if (score < best.cost) {
                    best.vi = vi;
                    best.ti = -1; // new trip
                    best.pos = 0;
                    best.cost = score;
                    best.eval = std::move(eval);
                }
            }
        }
    }

    return best;
}

void insertNode(ALNSSolution& sol, int node_index, const InsertionOption& opt,
                 const solver::SolveRequest& req, double fixed, double km, int reload_min) {
    auto it = std::find(sol.unrouted.begin(), sol.unrouted.end(), node_index);
    if (it != sol.unrouted.end()) sol.unrouted.erase(it);

    auto& vt = sol.vehicles[opt.vi];
    if (opt.ti < 0) {
        // New trip — already validated in findBestInsertion
        vt.trips.push_back(std::move(opt.eval.next));
    } else {
        // Existing trip — validate full schedule after modification
        auto original_state = vt.trips[opt.ti];
        vt.trips[opt.ti] = std::move(opt.eval.next);

        std::vector<std::vector<int>> all_trips;
        for (const auto& t : vt.trips) all_trips.push_back(t.nodes);
        const auto& vehicle = req.vehicles(vt.vehicle_index);
        auto val = validateTrips(req, vehicle, all_trips, fixed, km, reload_min);
        if (!val.feasible || !std::isfinite(val.total_cost)) {
            vt.trips[opt.ti] = std::move(original_state); // rollback
            sol.unrouted.push_back(node_index);
        }
    }

    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size());
}

} // namespace

void greedyRepair(ALNSSolution& sol, const solver::SolveRequest& req,
                  double fixed, double km, int reload_min)
{
    auto order = sortUnrouted(sol.unrouted, req);

    for (int node_index : order) {
        auto opt = findBestInsertion(sol, node_index, req, fixed, km, reload_min);
        if (opt.vi >= 0) {
            insertNode(sol, node_index, opt, req, fixed, km, reload_min);
        }
    }
}

void priorityFirstRepair(ALNSSolution& sol, const solver::SolveRequest& req,
                          double fixed, double km, int reload_min)
{
    std::vector<int> order = sol.unrouted;
    std::sort(order.begin(), order.end(), [&](int a, int b) {
        return req.nodes(a - 1).priority() > req.nodes(b - 1).priority();
    });

    for (int node_index : order) {
        auto opt = findBestInsertion(sol, node_index, req, fixed, km, reload_min);
        if (opt.vi >= 0) {
            insertNode(sol, node_index, opt, req, fixed, km, reload_min);
        }
    }
}

void regret2Repair(ALNSSolution& sol, const solver::SolveRequest& req,
                    double fixed, double km, int reload_min)
{
    std::vector<int> remaining = sol.unrouted;

    while (!remaining.empty()) {
        struct RegretCandidate {
            int node_index;
            double regret;
            InsertionOption best_opt;
        };

        std::vector<RegretCandidate> regrets;
        for (int ni : remaining) {
            // Find top 2 insertion options
            double best1 = std::numeric_limits<double>::infinity();
            double best2 = std::numeric_limits<double>::infinity();
            InsertionOption best_opt;

            for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
                const auto& vt = sol.vehicles[vi];
                const auto& vehicle = req.vehicles(vt.vehicle_index);

                for (int ti = 0; ti < (int)vt.trips.size(); ++ti) {
                    const auto& trip = vt.trips[ti];
                    for (int pos = 0; pos <= (int)trip.nodes.size(); ++pos) {
                        auto eval = evaluateInsertion(req, vehicle, trip, ni, pos, fixed, km);
                        if (eval.feasible && std::isfinite(eval.delta_cost)) {
                            double score = eval.delta_cost - priorityBonus(req.nodes(ni - 1));
                            if (score < best1) {
                                best2 = best1;
                                best1 = score;
                                best_opt.vi = vi;
                                best_opt.ti = ti;
                                best_opt.pos = pos;
                                best_opt.cost = score;
                                best_opt.eval = std::move(eval);
                            } else if (score < best2) {
                                best2 = score;
                            }
                        }
                    }
                }

                // New trip
                RouteState empty_route;
                empty_route.vehicle_index = vt.vehicle_index;
                auto eval = evaluateInsertion(req, vehicle, empty_route, ni, 0, fixed, km);
                if (eval.feasible && std::isfinite(eval.next.cost)) {
                    // Validate full multi-trip schedule
                    std::vector<std::vector<int>> all_trips;
                    double existing_cost = 0.0;
                    for (const auto& t : vt.trips) {
                        all_trips.push_back(t.nodes);
                        existing_cost += t.cost;
                    }
                    all_trips.push_back(eval.next.nodes);

                    auto full_val = validateTrips(req, vehicle, all_trips, fixed, km, reload_min);
                    if (full_val.feasible && std::isfinite(full_val.total_cost)) {
                        double score = full_val.total_cost - existing_cost
                                     - priorityBonus(req.nodes(ni - 1));
                        if (score < best1) {
                            best2 = best1;
                            best1 = score;
                            best_opt.vi = vi;
                            best_opt.ti = -1;
                            best_opt.pos = 0;
                            best_opt.cost = score;
                            best_opt.eval = std::move(eval);
                        } else if (score < best2) {
                            best2 = score;
                        }
                    }
                }
            }

            if (best_opt.vi >= 0) {
                double regret = (best2 < std::numeric_limits<double>::infinity())
                    ? best2 - best1 : best1;
                regrets.push_back({ni, regret, std::move(best_opt)});
            }
        }

        if (regrets.empty()) break;

        // Pick highest regret
        auto& best = *std::max_element(regrets.begin(), regrets.end(),
            [](const RegretCandidate& a, const RegretCandidate& b) {
                return a.regret < b.regret;
            });

        insertNode(sol, best.node_index, best.best_opt, req, fixed, km, reload_min);

        // Remove from remaining
        auto it = std::find(remaining.begin(), remaining.end(), best.node_index);
        if (it != remaining.end()) remaining.erase(it);
    }
}

void proactiveBreakInsertion(ALNSSolution& sol, const solver::SolveRequest& req,
                              double fixed, double km, int reload_min)
{
    static constexpr int MAX_CONTINUOUS_DRIVE = 210;

    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        const auto& vehicle = req.vehicles(sol.vehicles[vi].vehicle_index);
        if (vehicle.break_start() <= 0) continue;

        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            auto& trip = sol.vehicles[vi].trips[ti];
            if (trip.nodes.size() < 2) continue;

            int cumulative = 0;
            int worst_pos = -1;
            int worst_excess = 0;
            int ms = req.matrix_size();

            for (int pos = 1; pos < (int)trip.nodes.size(); ++pos) {
                cumulative += (int)req.durations(trip.nodes[pos - 1] * ms + trip.nodes[pos]);
                if (cumulative > MAX_CONTINUOUS_DRIVE) {
                    int excess = cumulative - MAX_CONTINUOUS_DRIVE;
                    if (excess > worst_excess) {
                        worst_excess = excess;
                        worst_pos = pos;
                    }
                }
            }

            if (worst_pos > 0 && worst_pos < (int)trip.nodes.size()) {
                // Split trip at worst_pos into two trips
                std::vector<int> first(trip.nodes.begin(), trip.nodes.begin() + worst_pos);
                std::vector<int> second(trip.nodes.begin() + worst_pos, trip.nodes.end());

                RouteState init;
                init.vehicle_index = sol.vehicles[vi].vehicle_index;
                auto eval1 = evaluateRouteState(req, vehicle, init, first, fixed, km);
                auto eval2 = evaluateRouteState(req, vehicle, init, second, fixed, km);

                if (eval1.feasible && eval2.feasible
                    && std::isfinite(eval1.next.cost) && std::isfinite(eval2.next.cost)) {
                    sol.vehicles[vi].trips[ti] = std::move(eval1.next);
                    sol.vehicles[vi].trips.insert(
                        sol.vehicles[vi].trips.begin() + ti + 1,
                        std::move(eval2.next));
                }
            }
        }
    }

    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size());
}

} // namespace alns
} // namespace hfvrptwb
