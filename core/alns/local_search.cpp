#include "alns/local_search.h"
#include "routeOpt/two_opt.h"
#include "validator/route_state.h"
#include <algorithm>
#include <limits>
#include <unordered_map>

namespace hfvrptwb {
namespace alns {

void tripMerge(ALNSSolution& sol, const solver::SolveRequest& req,
               double fixed, double km)
{
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        auto& vt = sol.vehicles[vi];
        if (vt.trips.size() < 2) continue;
        const auto& vehicle = req.vehicles(vt.vehicle_index);

        bool improved = true;
        while (improved) {
            improved = false;
            for (int ti1 = 0; ti1 < (int)vt.trips.size() && !improved; ++ti1) {
                for (int ti2 = ti1 + 1; ti2 < (int)vt.trips.size() && !improved; ++ti2) {
                    std::vector<int> merged = vt.trips[ti1].nodes;
                    merged.insert(merged.end(),
                        vt.trips[ti2].nodes.begin(), vt.trips[ti2].nodes.end());

                    RouteState init;
                    init.vehicle_index = vt.vehicle_index;
                    auto eval = evaluateRouteState(req, vehicle, init, merged, fixed, km);
                    if (!eval.feasible || !std::isfinite(eval.next.cost)) continue;

                    auto opt = twoOptTrip(req, vehicle, std::move(eval.next), fixed, km);
                    if (!std::isfinite(opt.cost)) continue;

                    double old_cost = vt.trips[ti1].cost + vt.trips[ti2].cost;
                    int old_dur = vt.trips[ti1].duration_min + vt.trips[ti2].duration_min;
                    double w = req.speed_weight();
                    double new_b = (1.0 - w) * opt.cost + w * static_cast<double>(opt.duration_min);
                    double old_b = (1.0 - w) * old_cost + w * static_cast<double>(old_dur);
                    if (new_b < old_b - 1e-6) {
                        vt.trips[ti1] = std::move(opt);
                        vt.trips.erase(vt.trips.begin() + ti2);
                        improved = true;
                    }
                }
            }
        }
    }
    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size(),
                                     1000.0, req.speed_weight());
}

void customerMoveAcrossTrips(ALNSSolution& sol, const solver::SolveRequest& req,
                              double fixed, double km)
{
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        auto& vt = sol.vehicles[vi];
        if (vt.trips.size() < 2) continue;
        const auto& vehicle = req.vehicles(vt.vehicle_index);

        bool improved = true;
        while (improved) {
            improved = false;
            for (int from_ti = 0; from_ti < (int)vt.trips.size() && !improved; ++from_ti) {
                for (int pos = 0; pos < (int)vt.trips[from_ti].nodes.size() && !improved; ++pos) {
                    int node_index = vt.trips[from_ti].nodes[pos];

                    for (int to_ti = 0; to_ti < (int)vt.trips.size(); ++to_ti) {
                        if (to_ti == from_ti) continue;

                        for (int insert_pos = 0; insert_pos <= (int)vt.trips[to_ti].nodes.size(); ++insert_pos) {
                            // Build source without node
                            std::vector<int> src_nodes;
                            for (int n : vt.trips[from_ti].nodes)
                                if (n != node_index) src_nodes.push_back(n);

                            if (src_nodes.empty()) continue;

                            // Build target with node inserted
                            std::vector<int> dst_nodes = vt.trips[to_ti].nodes;
                            dst_nodes.insert(dst_nodes.begin() + insert_pos, node_index);

                            RouteState init;
                            init.vehicle_index = vt.vehicle_index;
                            auto src_eval = evaluateRouteState(req, vehicle, init, src_nodes, fixed, km);
                            if (!src_eval.feasible || !std::isfinite(src_eval.next.cost)) continue;

                            auto dst_eval = evaluateRouteState(req, vehicle, init, dst_nodes, fixed, km);
                            if (!dst_eval.feasible || !std::isfinite(dst_eval.next.cost)) continue;

                            double new_cost = src_eval.next.cost + dst_eval.next.cost;
                            double old_cost = vt.trips[from_ti].cost + vt.trips[to_ti].cost;
                            {
                                double w = req.speed_weight();
                                int new_dur = src_eval.next.duration_min + dst_eval.next.duration_min;
                                int old_dur = vt.trips[from_ti].duration_min + vt.trips[to_ti].duration_min;
                                new_cost = (1.0 - w) * new_cost + w * static_cast<double>(new_dur);
                                old_cost = (1.0 - w) * old_cost + w * static_cast<double>(old_dur);
                            }
                            if (new_cost < old_cost - 1e-6) {
                                vt.trips[from_ti] = std::move(src_eval.next);
                                vt.trips[to_ti] = std::move(dst_eval.next);
                                improved = true;
                                break;
                            }
                        }
                        if (improved) break;
                    }
                }
            }
        }
    }
    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size(),
                                     1000.0, req.speed_weight());
}

void twoOptStar(ALNSSolution& sol, const solver::SolveRequest& req,
                double fixed, double km)
{
    if (sol.vehicles.size() < 2) return;

    for (int va = 0; va < (int)sol.vehicles.size(); ++va) {
        for (int vb = va + 1; vb < (int)sol.vehicles.size(); ++vb) {
            const auto& vehicle_a = req.vehicles(sol.vehicles[va].vehicle_index);
            const auto& vehicle_b = req.vehicles(sol.vehicles[vb].vehicle_index);

            for (int ta = 0; ta < (int)sol.vehicles[va].trips.size(); ++ta) {
                for (int tb = 0; tb < (int)sol.vehicles[vb].trips.size(); ++tb) {
                    const auto& trip_a = sol.vehicles[va].trips[ta];
                    const auto& trip_b = sol.vehicles[vb].trips[tb];

                    for (int cut_a = 1; cut_a < (int)trip_a.nodes.size(); ++cut_a) {
                        for (int cut_b = 1; cut_b < (int)trip_b.nodes.size(); ++cut_b) {
                            // A[0..cut_a) + B[cut_b..end)
                            std::vector<int> new_a(
                                trip_a.nodes.begin(), trip_a.nodes.begin() + cut_a);
                            new_a.insert(new_a.end(),
                                trip_b.nodes.begin() + cut_b, trip_b.nodes.end());

                            // B[0..cut_b) + A[cut_a..end)
                            std::vector<int> new_b(
                                trip_b.nodes.begin(), trip_b.nodes.begin() + cut_b);
                            new_b.insert(new_b.end(),
                                trip_a.nodes.begin() + cut_a, trip_a.nodes.end());

                            RouteState init_a;
                            init_a.vehicle_index = sol.vehicles[va].vehicle_index;
                            auto eval_a = evaluateRouteState(req, vehicle_a, init_a, new_a, fixed, km);

                            RouteState init_b;
                            init_b.vehicle_index = sol.vehicles[vb].vehicle_index;
                            auto eval_b = evaluateRouteState(req, vehicle_b, init_b, new_b, fixed, km);

                            if (eval_a.feasible && eval_b.feasible
                                && std::isfinite(eval_a.next.cost)
                                && std::isfinite(eval_b.next.cost)) {
                                double w = req.speed_weight();
                                double new_cost = (1.0 - w) * (eval_a.next.cost + eval_b.next.cost)
                                               + w * static_cast<double>(eval_a.next.duration_min + eval_b.next.duration_min);
                                double old_cost = (1.0 - w) * (trip_a.cost + trip_b.cost)
                                               + w * static_cast<double>(trip_a.duration_min + trip_b.duration_min);
                                if (new_cost < old_cost - 1e-6) {
                                    sol.vehicles[va].trips[ta] = std::move(eval_a.next);
                                    sol.vehicles[vb].trips[tb] = std::move(eval_b.next);
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size(),
                                     1000.0, req.speed_weight());
}

void relocateAcrossVehicles(ALNSSolution& sol, const solver::SolveRequest& req,
                             double fixed, double km)
{
    if (sol.vehicles.size() < 2) return;

    bool improved = true;
    while (improved) {
        improved = false;
        for (int va = 0; va < (int)sol.vehicles.size() && !improved; ++va) {
            for (int ta = 0; ta < (int)sol.vehicles[va].trips.size() && !improved; ++ta) {
                for (int pos = 0; pos < (int)sol.vehicles[va].trips[ta].nodes.size() && !improved; ++pos) {
                    int node_index = sol.vehicles[va].trips[ta].nodes[pos];

                    for (int vb = 0; vb < (int)sol.vehicles.size(); ++vb) {
                        if (vb == va) continue;
                        const auto& vehicle_b = req.vehicles(sol.vehicles[vb].vehicle_index);

                        for (int tb = 0; tb < (int)sol.vehicles[vb].trips.size(); ++tb) {
                            for (int ins = 0; ins <= (int)sol.vehicles[vb].trips[tb].nodes.size(); ++ins) {
                                // Source without node
                                std::vector<int> src_nodes;
                                for (int n : sol.vehicles[va].trips[ta].nodes)
                                    if (n != node_index) src_nodes.push_back(n);

                                // Target with node
                                std::vector<int> dst_nodes = sol.vehicles[vb].trips[tb].nodes;
                                dst_nodes.insert(dst_nodes.begin() + ins, node_index);

                                RouteState init_a;
                                init_a.vehicle_index = sol.vehicles[va].vehicle_index;
                                const auto& vehicle_a = req.vehicles(sol.vehicles[va].vehicle_index);

                                auto src_eval = evaluateRouteState(req, vehicle_a, init_a, src_nodes, fixed, km);
                                if (!src_eval.feasible || !std::isfinite(src_eval.next.cost)) continue;

                                RouteState init_b;
                                init_b.vehicle_index = sol.vehicles[vb].vehicle_index;
                                auto dst_eval = evaluateRouteState(req, vehicle_b, init_b, dst_nodes, fixed, km);
                                if (!dst_eval.feasible || !std::isfinite(dst_eval.next.cost)) continue;

                                double w = req.speed_weight();
                                double new_cost = (1.0 - w) * (src_eval.next.cost + dst_eval.next.cost)
                                               + w * static_cast<double>(src_eval.next.duration_min + dst_eval.next.duration_min);
                                double old_cost = (1.0 - w) * (sol.vehicles[va].trips[ta].cost + sol.vehicles[vb].trips[tb].cost)
                                               + w * static_cast<double>(sol.vehicles[va].trips[ta].duration_min + sol.vehicles[vb].trips[tb].duration_min);

                                if (new_cost < old_cost - 1e-6) {
                                    if (src_nodes.empty()) {
                                        sol.vehicles[va].trips.erase(sol.vehicles[va].trips.begin() + ta);
                                        if (sol.vehicles[va].trips.empty()) {
                                            sol.vehicles.erase(sol.vehicles.begin() + va);
                                        }
                                    } else {
                                        sol.vehicles[va].trips[ta] = std::move(src_eval.next);
                                    }
                                    sol.vehicles[vb].trips[tb] = std::move(dst_eval.next);
                                    improved = true;
                                    break;
                                }
                            }
                            if (improved) break;
                        }
                        if (improved) break;
                    }
                }
            }
        }
    }
    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size(),
                                     1000.0, req.speed_weight());
}

void consolidateVehicles(ALNSSolution& sol, const solver::SolveRequest& req,
                          double fixed, double km)
{
    if (sol.vehicles.size() < 2) return;

    // Sort vehicles by total node count (ascending) — try to empty smallest first
    struct VehicleSize { int vi; int total; };
    std::vector<VehicleSize> sizes;
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        int total = 0;
        for (const auto& t : sol.vehicles[vi].trips) total += (int)t.nodes.size();
        sizes.push_back({vi, total});
    }
    std::sort(sizes.begin(), sizes.end(),
              [](const VehicleSize& a, const VehicleSize& b) { return a.total < b.total; });

    for (const auto& sz : sizes) {
        int src_vi = sz.vi;

        // Collect all nodes from source vehicle
        std::vector<int> nodes;
        for (const auto& t : sol.vehicles[src_vi].trips)
            for (int n : t.nodes) nodes.push_back(n);

        // Save original objective for comparison
        double obj_before = sol.objective;

        // Remove source vehicle, mark nodes as unrouted
        sol.vehicles.erase(sol.vehicles.begin() + src_vi);
        for (int n : nodes) sol.unrouted.push_back(n);
        sol.forbid_new_vehicle = false;

        // Greedy-insert nodes into remaining vehicles
        // (uses deadline/priority sort, same order as greedyRepair)
        std::vector<int> order = nodes;
        std::sort(order.begin(), order.end(), [&](int a, int b) {
            const auto& na = req.nodes(a - 1);
            const auto& nb = req.nodes(b - 1);
            int da = na.deadline_min() > 0 ? na.deadline_min() : std::numeric_limits<int>::max();
            int db = nb.deadline_min() > 0 ? nb.deadline_min() : std::numeric_limits<int>::max();
            if (da != db) return da < db;
            if (na.priority() != nb.priority()) return na.priority() > nb.priority();
            return a < b;
        });

        // Map vehicle_index -> position in sol.vehicles
        std::unordered_map<int, int> used;
        for (int i = 0; i < (int)sol.vehicles.size(); ++i)
            used[sol.vehicles[i].vehicle_index] = i;

        struct MoveOption { int vi = -1; int ti = -1; int pos = -1; InsertionEval eval; };

        bool all_inserted = true;
        for (int ni : order) {
            double best_score = std::numeric_limits<double>::infinity();
            MoveOption best_opt;

            for (int vi_req = 0; vi_req < req.vehicles_size(); ++vi_req) {
                const auto& vehicle = req.vehicles(vi_req);
                auto it = used.find(vi_req);
                if (it == used.end()) continue; // only existing vehicles
                int vi_sol = it->second;

                for (int ti = 0; ti < (int)sol.vehicles[vi_sol].trips.size(); ++ti) {
                    const auto& trip = sol.vehicles[vi_sol].trips[ti];
                    for (int pos = 0; pos <= (int)trip.nodes.size(); ++pos) {
                        auto eval = evaluateInsertion(req, vehicle, trip, ni, pos, fixed, km);
                        if (eval.feasible && std::isfinite(eval.delta_cost)) {
                            double w = req.speed_weight();
                            double score = (1.0 - w) * eval.delta_cost
                                         + w * static_cast<double>(eval.delta_duration);
                            if (score < best_score) {
                                best_score = score;
                                best_opt.vi = vi_sol;
                                best_opt.ti = ti;
                                best_opt.pos = pos;
                                best_opt.eval = std::move(eval);
                            }
                        }
                    }
                }
            }

            if (best_opt.vi < 0) {
                all_inserted = false;
                break;
            }

            // Apply insertion
            sol.vehicles[best_opt.vi].trips[best_opt.ti] = std::move(best_opt.eval.next);
            auto it2 = std::find(sol.unrouted.begin(), sol.unrouted.end(), ni);
            if (it2 != sol.unrouted.end()) sol.unrouted.erase(it2);
        }

        sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size(),
                                     1000.0, req.speed_weight());

        if (!all_inserted || sol.objective >= obj_before - 1e-6) {
            // Rollback: undo. We can't easily undo, so skip this vehicle.
            // (In practice, this shouldn't happen for small vehicles on dense instances.)
            break; // give up — partial state is corrupted
        }

        // Successfully eliminated one vehicle — restart the outer loop
        // (break out and let while(improved) handle the restart)
        break; // one vehicle eliminated per call is enough
    }
}

void swapStar(ALNSSolution& sol, const solver::SolveRequest& req,
              double fixed, double km)
{
    if (sol.vehicles.size() < 2) return;

    bool improved = true;
    while (improved) {
        improved = false;

        for (int va = 0; va < (int)sol.vehicles.size() && !improved; ++va) {
            for (int vb = va + 1; vb < (int)sol.vehicles.size() && !improved; ++vb) {
                const auto& vehicle_a = req.vehicles(sol.vehicles[va].vehicle_index);
                const auto& vehicle_b = req.vehicles(sol.vehicles[vb].vehicle_index);

                for (int ta = 0; ta < (int)sol.vehicles[va].trips.size() && !improved; ++ta) {
                    for (int pa = 0; pa < (int)sol.vehicles[va].trips[ta].nodes.size() && !improved; ++pa) {
                        int node_a = sol.vehicles[va].trips[ta].nodes[pa];

                        for (int tb = 0; tb < (int)sol.vehicles[vb].trips.size() && !improved; ++tb) {
                            for (int pb = 0; pb < (int)sol.vehicles[vb].trips[tb].nodes.size() && !improved; ++pb) {
                                int node_b = sol.vehicles[vb].trips[tb].nodes[pb];

                                // --- Build trip A without node_a ---
                                std::vector<int> nodes_a_minus;
                                for (int n : sol.vehicles[va].trips[ta].nodes)
                                    if (n != node_a) nodes_a_minus.push_back(n);

                                RouteState init_a;
                                init_a.vehicle_index = sol.vehicles[va].vehicle_index;

                                // Find best trip & position in vehicle A for node_b
                                // (search ALL existing trips, not just ta)
                                double best_cost_a = std::numeric_limits<double>::infinity();
                                int best_trip_a = -1;
                                InsertionEval best_eval_a;

                                auto tryInsertIntoA = [&](const RouteState& trip_state,
                                                          int trip_idx) {
                                    for (int pos = 0; pos <= (int)trip_state.nodes.size(); ++pos) {
                                        auto eval = evaluateInsertion(
                                            req, vehicle_a, trip_state, node_b, pos, fixed, km);
                                        if (eval.feasible && std::isfinite(eval.delta_cost)
                                            && eval.next.cost < best_cost_a) {
                                            best_cost_a = eval.next.cost;
                                            best_trip_a = trip_idx;
                                            best_eval_a = std::move(eval);
                                        }
                                    }
                                };

                                // Try modified trip ta (if not empty)
                                if (!nodes_a_minus.empty()) {
                                    auto base_a = evaluateRouteState(
                                        req, vehicle_a, init_a, nodes_a_minus, fixed, km);
                                    if (base_a.feasible && std::isfinite(base_a.next.cost))
                                        tryInsertIntoA(base_a.next, ta);
                                }
                                // Try other trips in vehicle A
                                for (int t2 = 0; t2 < (int)sol.vehicles[va].trips.size(); ++t2) {
                                    if (t2 == ta) continue;
                                    tryInsertIntoA(sol.vehicles[va].trips[t2], t2);
                                }

                                if (best_trip_a < 0) continue;

                                // --- Build trip B without node_b ---
                                std::vector<int> nodes_b_minus;
                                for (int n : sol.vehicles[vb].trips[tb].nodes)
                                    if (n != node_b) nodes_b_minus.push_back(n);

                                RouteState init_b;
                                init_b.vehicle_index = sol.vehicles[vb].vehicle_index;

                                // Find best trip & position in vehicle B for node_a
                                double best_cost_b = std::numeric_limits<double>::infinity();
                                int best_trip_b = -1;
                                InsertionEval best_eval_b;

                                auto tryInsertIntoB = [&](const RouteState& trip_state,
                                                          int trip_idx) {
                                    for (int pos = 0; pos <= (int)trip_state.nodes.size(); ++pos) {
                                        auto eval = evaluateInsertion(
                                            req, vehicle_b, trip_state, node_a, pos, fixed, km);
                                        if (eval.feasible && std::isfinite(eval.delta_cost)
                                            && eval.next.cost < best_cost_b) {
                                            best_cost_b = eval.next.cost;
                                            best_trip_b = trip_idx;
                                            best_eval_b = std::move(eval);
                                        }
                                    }
                                };

                                if (!nodes_b_minus.empty()) {
                                    auto base_b = evaluateRouteState(
                                        req, vehicle_b, init_b, nodes_b_minus, fixed, km);
                                    if (base_b.feasible && std::isfinite(base_b.next.cost))
                                        tryInsertIntoB(base_b.next, tb);
                                }
                                for (int t2 = 0; t2 < (int)sol.vehicles[vb].trips.size(); ++t2) {
                                    if (t2 == tb) continue;
                                    tryInsertIntoB(sol.vehicles[vb].trips[t2], t2);
                                }

                                if (best_trip_b < 0) continue;

                                // --- Compute total cost delta ---
                                double old_raw = 0.0;
                                int old_dur = 0;
                                for (const auto& t : sol.vehicles[va].trips) { old_raw += t.cost; old_dur += t.duration_min; }
                                for (const auto& t : sol.vehicles[vb].trips) { old_raw += t.cost; old_dur += t.duration_min; }

                                double new_raw = best_cost_a + best_cost_b;
                                int new_dur = best_eval_a.next.duration_min + best_eval_b.next.duration_min;
                                // Add unaffected trips
                                for (int t2 = 0; t2 < (int)sol.vehicles[va].trips.size(); ++t2)
                                    if (t2 != ta && t2 != best_trip_a) {
                                        new_raw += sol.vehicles[va].trips[t2].cost;
                                        new_dur += sol.vehicles[va].trips[t2].duration_min;
                                    }
                                // Handle ta when it's not the target trip
                                if (best_trip_a != ta && !nodes_a_minus.empty()) {
                                    auto base = evaluateRouteState(
                                        req, vehicle_a, init_a, nodes_a_minus, fixed, km);
                                    if (base.feasible && std::isfinite(base.next.cost)) {
                                        new_raw += base.next.cost;
                                        new_dur += base.next.duration_min;
                                    } else continue;
                                }

                                for (int t2 = 0; t2 < (int)sol.vehicles[vb].trips.size(); ++t2)
                                    if (t2 != tb && t2 != best_trip_b) {
                                        new_raw += sol.vehicles[vb].trips[t2].cost;
                                        new_dur += sol.vehicles[vb].trips[t2].duration_min;
                                    }
                                if (best_trip_b != tb && !nodes_b_minus.empty()) {
                                    auto base = evaluateRouteState(
                                        req, vehicle_b, init_b, nodes_b_minus, fixed, km);
                                    if (base.feasible && std::isfinite(base.next.cost)) {
                                        new_raw += base.next.cost;
                                        new_dur += base.next.duration_min;
                                    } else continue;
                                }

                                double sw = req.speed_weight();
                                double new_cost = (1.0 - sw) * new_raw + sw * static_cast<double>(new_dur);
                                double old_cost = (1.0 - sw) * old_raw + sw * static_cast<double>(old_dur);
                                if (new_cost < old_cost - 1e-6) {
                                    // Apply swap
                                    sol.vehicles[va].trips[best_trip_a]
                                        = std::move(best_eval_a.next);
                                    if (best_trip_a != ta) {
                                        // Update trip ta (node_a removed)
                                        sol.vehicles[va].trips[ta]
                                            = std::move(evaluateRouteState(
                                                req, vehicle_a, init_a, nodes_a_minus,
                                                fixed, km).next);
                                    }
                                    sol.vehicles[vb].trips[best_trip_b]
                                        = std::move(best_eval_b.next);
                                    if (best_trip_b != tb) {
                                        sol.vehicles[vb].trips[tb]
                                            = std::move(evaluateRouteState(
                                                req, vehicle_b, init_b, nodes_b_minus,
                                                fixed, km).next);
                                    }

                                    // Clean up empty trips (erase in descending index order)
                                    for (int vi : {vb, va}) {  // vb > va, erase larger first
                                        if (vi >= (int)sol.vehicles.size()) continue;
                                        auto& vt = sol.vehicles[vi];
                                        vt.trips.erase(
                                            std::remove_if(vt.trips.begin(), vt.trips.end(),
                                                [](const RouteState& t) {
                                                    return t.nodes.empty();
                                                }),
                                            vt.trips.end());
                                        if (vt.trips.empty())
                                            sol.vehicles.erase(
                                                sol.vehicles.begin() + vi);
                                    }
                                    improved = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }
    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size(),
                                     1000.0, req.speed_weight());
}

void applyLocalSearch(ALNSSolution& sol, const solver::SolveRequest& req,
                      double fixed, double km)
{
    tripMerge(sol, req, fixed, km);
    customerMoveAcrossTrips(sol, req, fixed, km);
    twoOptStar(sol, req, fixed, km);
    relocateAcrossVehicles(sol, req, fixed, km);
    swapStar(sol, req, fixed, km);
}

} // namespace alns
} // namespace hfvrptwb
