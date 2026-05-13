#include "alns/repair.h"
#include "validator/route_state.h"
#include "validator/route_validator.h"
#include <algorithm>
#include <limits>
#include <unordered_map>

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
    int vi = -1;      // index in sol.vehicles, or -1
    int ti = -1;      // trip index, -1 for new trip
    int pos = -1;     // insertion position
    double cost = std::numeric_limits<double>::infinity();
    int vehicle_index = -1;  // actual vehicle index from req (used when creating new vehicle)
    bool new_vehicle = false;
    InsertionEval eval;
};

InsertionOption findBestInsertion(
    const ALNSSolution& sol, int node_index,
    const solver::SolveRequest& req, double fixed, double km, int reload_min)
{
    InsertionOption best;

    // Map vehicle_index -> position in sol.vehicles
    std::unordered_map<int, int> used;
    for (int i = 0; i < (int)sol.vehicles.size(); ++i) {
        used[sol.vehicles[i].vehicle_index] = i;
    }

    for (int vi_req = 0; vi_req < req.vehicles_size(); ++vi_req) {
        const auto& vehicle = req.vehicles(vi_req);
        auto it = used.find(vi_req);
        bool in_solution = (it != used.end());
        int vi_sol = in_solution ? it->second : -1;

        // Try existing trips (only for vehicles already in solution)
        if (in_solution) {
            const auto& vt = sol.vehicles[vi_sol];
            for (int ti = 0; ti < (int)vt.trips.size(); ++ti) {
                const auto& trip = vt.trips[ti];
                for (int pos = 0; pos <= (int)trip.nodes.size(); ++pos) {
                    auto eval = evaluateInsertion(req, vehicle, trip, node_index, pos, fixed, km);
                    if (eval.feasible && std::isfinite(eval.delta_cost)) {
                        double score = eval.delta_cost - priorityBonus(req.nodes(node_index - 1));
                        if (score < best.cost) {
                            best.vi = vi_sol;
                            best.ti = ti;
                            best.pos = pos;
                            best.cost = score;
                            best.new_vehicle = false;
                            best.eval = std::move(eval);
                        }
                    }
                }
            }
        }

        // Try new trip in this vehicle
        RouteState empty_route;
        empty_route.vehicle_index = vi_req;
        auto eval = evaluateInsertion(req, vehicle, empty_route, node_index, 0, fixed, km);
        if (eval.feasible && std::isfinite(eval.next.cost)) {
            double score;
            if (in_solution) {
                // Validate full multi-trip schedule including reload time
                const auto& vt = sol.vehicles[vi_sol];
                std::vector<std::vector<int>> all_trips;
                double existing_cost = 0.0;
                for (const auto& t : vt.trips) {
                    all_trips.push_back(t.nodes);
                    existing_cost += t.cost;
                }
                all_trips.push_back(eval.next.nodes);

                auto full_val = validateTrips(req, vehicle, all_trips, fixed, km, reload_min);
                if (!full_val.feasible || !std::isfinite(full_val.total_cost)) continue;
                score = full_val.total_cost - existing_cost
                         - priorityBonus(req.nodes(node_index - 1));
                if (score < best.cost) {
                    best.vi = vi_sol;
                    best.ti = -1;
                    best.pos = 0;
                    best.cost = score;
                    best.new_vehicle = false;
                    best.eval = std::move(eval);
                }
            } else if (!sol.forbid_new_vehicle) {
                // Unused vehicle — single trip, no multi-trip validation needed
                score = eval.next.cost - priorityBonus(req.nodes(node_index - 1));
                if (score < best.cost) {
                    best.vehicle_index = vi_req;
                    best.ti = -1;
                    best.pos = 0;
                    best.cost = score;
                    best.new_vehicle = true;
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

    if (opt.new_vehicle) {
        // Create a brand-new vehicle entry
        VehicleTrips vt;
        vt.vehicle_index = opt.vehicle_index;
        vt.trips.push_back(std::move(opt.eval.next));
        sol.vehicles.push_back(std::move(vt));
        sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size());
        return;
    }

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
        if (opt.vi >= 0 || opt.new_vehicle) {
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
        if (opt.vi >= 0 || opt.new_vehicle) {
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

        // Map vehicle_index -> position in sol.vehicles
        std::unordered_map<int, int> used;
        for (int i = 0; i < (int)sol.vehicles.size(); ++i) {
            used[sol.vehicles[i].vehicle_index] = i;
        }

        std::vector<RegretCandidate> regrets;
        for (int ni : remaining) {
            // Find top 2 insertion options
            double best1 = std::numeric_limits<double>::infinity();
            double best2 = std::numeric_limits<double>::infinity();
            InsertionOption best_opt;

            auto updateBest = [&](double score, InsertionOption opt) {
                if (score < best1) {
                    best2 = best1;
                    best1 = score;
                    best_opt = std::move(opt);
                } else if (score < best2) {
                    best2 = score;
                }
            };

            for (int vi_req = 0; vi_req < req.vehicles_size(); ++vi_req) {
                const auto& vehicle = req.vehicles(vi_req);
                auto it = used.find(vi_req);
                bool in_solution = (it != used.end());
                int vi_sol = in_solution ? it->second : -1;

                // Try existing trips (only for vehicles in solution)
                if (in_solution) {
                    const auto& vt = sol.vehicles[vi_sol];
                    for (int ti = 0; ti < (int)vt.trips.size(); ++ti) {
                        const auto& trip = vt.trips[ti];
                        for (int pos = 0; pos <= (int)trip.nodes.size(); ++pos) {
                            auto eval = evaluateInsertion(req, vehicle, trip, ni, pos, fixed, km);
                            if (eval.feasible && std::isfinite(eval.delta_cost)) {
                                double score = eval.delta_cost - priorityBonus(req.nodes(ni - 1));
                                InsertionOption opt;
                                opt.vi = vi_sol;
                                opt.ti = ti;
                                opt.pos = pos;
                                opt.cost = score;
                                opt.new_vehicle = false;
                                opt.eval = std::move(eval);
                                updateBest(score, std::move(opt));
                            }
                        }
                    }
                }

                // Try new trip in this vehicle
                RouteState empty_route;
                empty_route.vehicle_index = vi_req;
                auto eval = evaluateInsertion(req, vehicle, empty_route, ni, 0, fixed, km);
                if (eval.feasible && std::isfinite(eval.next.cost)) {
                    double score;
                    InsertionOption opt;
                    opt.pos = 0;

                    if (in_solution) {
                        const auto& vt = sol.vehicles[vi_sol];
                        std::vector<std::vector<int>> all_trips;
                        double existing_cost = 0.0;
                        for (const auto& t : vt.trips) {
                            all_trips.push_back(t.nodes);
                            existing_cost += t.cost;
                        }
                        all_trips.push_back(eval.next.nodes);

                        auto full_val = validateTrips(req, vehicle, all_trips, fixed, km, reload_min);
                        if (!full_val.feasible || !std::isfinite(full_val.total_cost)) continue;
                        score = full_val.total_cost - existing_cost
                              - priorityBonus(req.nodes(ni - 1));
                        opt.vi = vi_sol;
                        opt.ti = -1;
                        opt.new_vehicle = false;
                    } else if (!sol.forbid_new_vehicle) {
                        score = eval.next.cost - priorityBonus(req.nodes(ni - 1));
                        opt.vehicle_index = vi_req;
                        opt.ti = -1;
                        opt.new_vehicle = true;
                    } else {
                        continue;
                    }
                    opt.cost = score;
                    opt.eval = std::move(eval);
                    updateBest(score, std::move(opt));
                }
            }

            if (best_opt.vi >= 0 || best_opt.new_vehicle) {
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

void regret3Repair(ALNSSolution& sol, const solver::SolveRequest& req,
                    double fixed, double km, int /*reload_min*/)
{
    std::vector<int> remaining = sol.unrouted;

    while (!remaining.empty()) {
        struct RegretCandidate {
            int node_index;
            double regret;
            InsertionOption best_opt;
        };

        // Map vehicle_index -> position in sol.vehicles
        std::unordered_map<int, int> used;
        for (int i = 0; i < (int)sol.vehicles.size(); ++i) {
            used[sol.vehicles[i].vehicle_index] = i;
        }

        std::vector<RegretCandidate> regrets;
        for (int ni : remaining) {
            // Find top 3 insertion options
            double best1 = std::numeric_limits<double>::infinity();
            double best2 = std::numeric_limits<double>::infinity();
            double best3 = std::numeric_limits<double>::infinity();
            InsertionOption best_opt;

            auto updateBest = [&](double score, InsertionOption opt) {
                if (score < best1) {
                    best3 = best2;
                    best2 = best1;
                    best1 = score;
                    best_opt = std::move(opt);
                } else if (score < best2) {
                    best3 = best2;
                    best2 = score;
                } else if (score < best3) {
                    best3 = score;
                }
            };

            for (int vi_req = 0; vi_req < req.vehicles_size(); ++vi_req) {
                const auto& vehicle = req.vehicles(vi_req);
                auto it = used.find(vi_req);
                bool in_solution = (it != used.end());
                int vi_sol = in_solution ? it->second : -1;

                // Try existing trips (only for vehicles in solution)
                if (in_solution) {
                    const auto& vt = sol.vehicles[vi_sol];
                    for (int ti = 0; ti < (int)vt.trips.size(); ++ti) {
                        const auto& trip = vt.trips[ti];
                        for (int pos = 0; pos <= (int)trip.nodes.size(); ++pos) {
                            auto eval = evaluateInsertion(req, vehicle, trip, ni, pos, fixed, km);
                            if (eval.feasible && std::isfinite(eval.delta_cost)) {
                                double score = eval.delta_cost - priorityBonus(req.nodes(ni - 1));
                                InsertionOption opt;
                                opt.vi = vi_sol;
                                opt.ti = ti;
                                opt.pos = pos;
                                opt.cost = score;
                                opt.new_vehicle = false;
                                opt.eval = std::move(eval);
                                updateBest(score, std::move(opt));
                            }
                        }
                    }
                }

                // Try new trip in this vehicle
                RouteState empty_route;
                empty_route.vehicle_index = vi_req;
                auto eval = evaluateInsertion(req, vehicle, empty_route, ni, 0, fixed, km);
                if (eval.feasible && std::isfinite(eval.next.cost)) {
                    double score;
                    InsertionOption opt;
                    opt.pos = 0;

                    if (in_solution) {
                        const auto& vt = sol.vehicles[vi_sol];
                        std::vector<std::vector<int>> all_trips;
                        double existing_cost = 0.0;
                        for (const auto& t : vt.trips) {
                            all_trips.push_back(t.nodes);
                            existing_cost += t.cost;
                        }
                        all_trips.push_back(eval.next.nodes);

                        auto full_val = validateTrips(req, vehicle, all_trips, fixed, km, 0);
                        if (!full_val.feasible || !std::isfinite(full_val.total_cost)) continue;
                        score = full_val.total_cost - existing_cost
                              - priorityBonus(req.nodes(ni - 1));
                        opt.vi = vi_sol;
                        opt.ti = -1;
                        opt.new_vehicle = false;
                    } else if (!sol.forbid_new_vehicle) {
                        score = eval.next.cost - priorityBonus(req.nodes(ni - 1));
                        opt.vehicle_index = vi_req;
                        opt.ti = -1;
                        opt.new_vehicle = true;
                    } else {
                        continue;
                    }
                    opt.cost = score;
                    opt.eval = std::move(eval);
                    updateBest(score, std::move(opt));
                }
            }

            if (best_opt.vi >= 0 || best_opt.new_vehicle) {
                double regret = best1;
                if (best2 < std::numeric_limits<double>::infinity())
                    regret = (best2 - best1);
                if (best3 < std::numeric_limits<double>::infinity())
                    regret += (best3 - best1);
                regrets.push_back({ni, regret, std::move(best_opt)});
            }
        }

        if (regrets.empty()) break;

        // Pick highest regret
        auto& best = *std::max_element(regrets.begin(), regrets.end(),
            [](const RegretCandidate& a, const RegretCandidate& b) {
                return a.regret < b.regret;
            });

        insertNode(sol, best.node_index, best.best_opt, req, fixed, km, 0);

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
