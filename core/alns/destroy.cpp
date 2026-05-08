#include "alns/destroy.h"
#include "feasibility/tags.h"
#include "priority_shape_clustering/slender_utils.h"
#include "validator/route_state.h"
#include <algorithm>
#include <cmath>
#include <unordered_set>

namespace hfvrptwb {
namespace alns {

void removeNode(ALNSSolution& sol, int vi, int ti, int node_index,
                const solver::SolveRequest& req, double fixed, double km)
{
    if (vi < 0 || vi >= (int)sol.vehicles.size()) return;
    auto& vt = sol.vehicles[vi];
    if (ti < 0 || ti >= (int)vt.trips.size()) return;
    auto& trip = vt.trips[ti];

    auto it = std::find(trip.nodes.begin(), trip.nodes.end(), node_index);
    if (it == trip.nodes.end()) return;
    trip.nodes.erase(it);

    if (trip.nodes.empty()) {
        vt.trips.erase(vt.trips.begin() + ti);
        if (vt.trips.empty()) {
            sol.vehicles.erase(sol.vehicles.begin() + vi);
        }
    } else {
        const auto& vehicle = req.vehicles(vt.vehicle_index);
        RouteState init;
        init.vehicle_index = vt.vehicle_index;
        auto eval = evaluateRouteState(req, vehicle, init, trip.nodes, fixed, km);
        if (eval.feasible && std::isfinite(eval.next.cost)) {
            trip = std::move(eval.next);
        } else {
            vt.trips.erase(vt.trips.begin() + ti);
            if (vt.trips.empty()) {
                sol.vehicles.erase(sol.vehicles.begin() + vi);
            }
        }
    }

    sol.unrouted.push_back(node_index);
    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size());
}

void randomRemoval(ALNSSolution& sol, std::mt19937& rng, const solver::SolveRequest& req,
                   double fixed, double km, int q)
{
    struct Slot { int vi; int ti; int pos; int node_index; };
    std::vector<Slot> slots;
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            const auto& nodes = sol.vehicles[vi].trips[ti].nodes;
            for (int pos = 0; pos < (int)nodes.size(); ++pos) {
                slots.push_back({vi, ti, pos, nodes[pos]});
            }
        }
    }
    std::shuffle(slots.begin(), slots.end(), rng);
    int count = std::min(q, (int)slots.size());
    for (int i = 0; i < count; ++i) {
        removeNode(sol, slots[i].vi, slots[i].ti, slots[i].node_index, req, fixed, km);
    }
}

void worstRemoval(ALNSSolution& sol, std::mt19937&, const solver::SolveRequest& req,
                  double fixed, double km, int q)
{
    struct Candidate { int vi; int ti; int node_index; double delta; };
    std::vector<Candidate> candidates;

    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        const auto& vehicle = req.vehicles(sol.vehicles[vi].vehicle_index);
        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            const auto& trip = sol.vehicles[vi].trips[ti];
            for (int node_index : trip.nodes) {
                std::vector<int> without;
                for (int n : trip.nodes) if (n != node_index) without.push_back(n);
                if (without.empty()) {
                    candidates.push_back({vi, ti, node_index, trip.cost});
                    continue;
                }
                RouteState init;
                init.vehicle_index = sol.vehicles[vi].vehicle_index;
                auto eval = evaluateRouteState(req, vehicle, init, without, fixed, km);
                double cost_without = eval.feasible ? eval.next.cost : 1e9;
                double delta = trip.cost - cost_without;
                candidates.push_back({vi, ti, node_index, delta});
            }
        }
    }

    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.delta > b.delta; });

    std::unordered_set<int> removed;
    int count = 0;
    for (const auto& c : candidates) {
        if (count >= q) break;
        if (removed.count(c.node_index)) continue;
        removed.insert(c.node_index);
        removeNode(sol, c.vi, c.ti, c.node_index, req, fixed, km);
        ++count;
    }
}

void shawRemoval(ALNSSolution& sol, std::mt19937& rng, const solver::SolveRequest& req,
                 double fixed, double km, int q)
{
    // Collect all nodes
    struct Slot { int vi; int ti; int node_index; };
    std::vector<Slot> all;
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            for (int ni : sol.vehicles[vi].trips[ti].nodes) {
                all.push_back({vi, ti, ni});
            }
        }
    }
    if (all.empty()) return;

    std::uniform_int_distribution<int> dist(0, (int)all.size() - 1);
    int seed_idx = dist(rng);
    int seed_node = all[seed_idx].node_index;
    const auto& seed_n = req.nodes(seed_node - 1);
    int ms = req.matrix_size();

    struct Scored { int vi; int ti; int node_index; double score; };
    std::vector<Scored> scored;
    for (const auto& s : all) {
        if (s.vi == all[seed_idx].vi && s.ti == all[seed_idx].ti && s.node_index == seed_node)
            continue;
        const auto& n = req.nodes(s.node_index - 1);
        double travel = req.distances(seed_node * ms + s.node_index);
        double tw_diff = std::abs((double)n.tw_end() - (double)seed_n.tw_end());
        double demand_diff = std::abs((double)n.demand() - (double)seed_n.demand());
        scored.push_back({s.vi, s.ti, s.node_index, travel + tw_diff + demand_diff});
    }

    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.score < b.score; });

    int count = std::min(q, (int)scored.size());
    for (int i = 0; i < count; ++i) {
        removeNode(sol, scored[i].vi, scored[i].ti, scored[i].node_index, req, fixed, km);
    }
}

void priorityAwareRemoval(ALNSSolution& sol, std::mt19937&, const solver::SolveRequest& req,
                           double fixed, double km, int q)
{
    struct Candidate { int vi; int ti; int node_index; int priority; };
    std::vector<Candidate> candidates;
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            for (int ni : sol.vehicles[vi].trips[ti].nodes) {
                candidates.push_back({vi, ti, ni, req.nodes(ni - 1).priority()});
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.priority < b.priority; });

    int count = std::min(q, (int)candidates.size());
    for (int i = 0; i < count; ++i) {
        removeNode(sol, candidates[i].vi, candidates[i].ti, candidates[i].node_index, req, fixed, km);
    }
}

void routeConsolidationDestroy(ALNSSolution& sol, std::mt19937&, const solver::SolveRequest& req,
                               double fixed, double km, int /*q*/)
{
    if (sol.vehicles.size() < 2) return;

    struct Size { int vi; int total; };
    std::vector<Size> sizes;
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        int total = 0;
        for (const auto& t : sol.vehicles[vi].trips) total += (int)t.nodes.size();
        sizes.push_back({vi, total});
    }
    std::sort(sizes.begin(), sizes.end(),
              [](const Size& a, const Size& b) { return a.total < b.total; });

    // Remove all nodes from 2 smallest vehicles (work backwards to preserve indices)
    int v1 = sizes[0].vi;
    int v2 = sizes[1].vi;
    for (int vi : {v2, v1}) {
        while (!sol.vehicles[vi].trips.empty()) {
            int ti = (int)sol.vehicles[vi].trips.size() - 1;
            while (!sol.vehicles[vi].trips[ti].nodes.empty()) {
                int ni = sol.vehicles[vi].trips[ti].nodes.back();
                removeNode(sol, vi, ti, ni, req, fixed, km);
                if (vi >= (int)sol.vehicles.size()) break;
            }
            if (vi < (int)sol.vehicles.size() && ti < (int)sol.vehicles[vi].trips.size()) {
                // trip was not auto-erased
            }
        }
    }
}

void tripRemoval(ALNSSolution& sol, std::mt19937&, const solver::SolveRequest& req,
                 double fixed, double km, int /*q*/)
{
    int best_vi = -1, best_ti = -1, best_size = 999999;
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            int sz = (int)sol.vehicles[vi].trips[ti].nodes.size();
            if (sz > 0 && sz < best_size) {
                best_size = sz;
                best_vi = vi;
                best_ti = ti;
            }
        }
    }
    if (best_vi < 0) return;

    auto nodes = sol.vehicles[best_vi].trips[best_ti].nodes;
    for (int ni : nodes) {
        removeNode(sol, best_vi, best_ti, ni, req, fixed, km);
    }
}

void tagViolationRemoval(ALNSSolution& sol, std::mt19937&, const solver::SolveRequest& req,
                          double fixed, double km, int /*q*/)
{
    struct Removal { int vi; int ti; int node_index; };
    std::vector<Removal> to_remove;

    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        const auto& vehicle = req.vehicles(sol.vehicles[vi].vehicle_index);
        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            for (int ni : sol.vehicles[vi].trips[ti].nodes) {
                const auto& node = req.nodes(ni - 1);
                if (!feasibility::isTagCompatible(vehicle, node)) {
                    to_remove.push_back({vi, ti, ni});
                }
            }
        }
    }

    for (const auto& r : to_remove) {
        removeNode(sol, r.vi, r.ti, r.node_index, req, fixed, km);
    }
}

void lateCustomerRemoval(ALNSSolution& sol, std::mt19937&, const solver::SolveRequest& req,
                          double fixed, double km, int q)
{
    struct Lateness { int vi; int ti; int node_index; int lateness; };
    std::vector<Lateness> late;

    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            const auto& trip = sol.vehicles[vi].trips[ti];
            for (int pos = 0; pos < (int)trip.forward_labels.size(); ++pos) {
                const auto& label = trip.forward_labels[pos];
                if (pos >= (int)trip.nodes.size()) break;
                int ni = trip.nodes[pos];
                const auto& node = req.nodes(ni - 1);
                int tw_end = node.tw_end() > 0 ? node.tw_end() : 1440;
                int arrival = label.earliest_arrival;
                if (arrival > tw_end) {
                    late.push_back({vi, ti, ni, arrival - tw_end});
                }
            }
        }
    }

    std::sort(late.begin(), late.end(),
              [](const Lateness& a, const Lateness& b) { return a.lateness > b.lateness; });

    int count = std::min(q, (int)late.size());
    for (int i = 0; i < count; ++i) {
        removeNode(sol, late[i].vi, late[i].ti, late[i].node_index, req, fixed, km);
    }
}

void sectorRemoval(ALNSSolution& sol, std::mt19937& rng, const solver::SolveRequest& req,
                   double fixed, double km, int q)
{
    struct Slot { int vi; int ti; int node_index; double theta; };
    std::vector<Slot> all;
    for (int vi = 0; vi < (int)sol.vehicles.size(); ++vi) {
        for (int ti = 0; ti < (int)sol.vehicles[vi].trips.size(); ++ti) {
            for (int ni : sol.vehicles[vi].trips[ti].nodes) {
                const auto& node = req.nodes(ni - 1);
                double theta = calculateAngle(
                    req.depot().lat(), req.depot().lng(),
                    node.lat(), node.lng());
                all.push_back({vi, ti, ni, theta});
            }
        }
    }
    if (all.empty()) return;

    // Pick a random seed and destroy q nodes closest in angular distance
    std::uniform_int_distribution<int> dist(0, (int)all.size() - 1);
    double seed_theta = all[dist(rng)].theta;

    struct Scored { int vi; int ti; int node_index; double dist; };
    std::vector<Scored> scored;
    for (const auto& s : all) {
        // Normalized angular distance from SC3: 0 = same bearing, 1 = opposite
        double diff = std::abs(seed_theta - s.theta);
        double ang_dist = (M_PI - std::abs(M_PI - diff)) / M_PI;
        scored.push_back({s.vi, s.ti, s.node_index, ang_dist});
    }

    if (scored.empty()) return;

    // Keep the seed node too — include all
    std::sort(scored.begin(), scored.end(),
              [](const Scored& a, const Scored& b) { return a.dist < b.dist; });

    int count = std::min(q, (int)scored.size());
    for (int i = 0; i < count; ++i) {
        removeNode(sol, scored[i].vi, scored[i].ti, scored[i].node_index, req, fixed, km);
    }
}

} // namespace alns
} // namespace hfvrptwb
