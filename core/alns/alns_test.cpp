#include "alns/solution.h"
#include "alns/adaptive_penalty.h"
#include "alns/destroy.h"
#include "alns/repair.h"
#include "alns/alns.h"
#include "construction/adaptive_constructor.h"
#include "validator/route_state.h"
#include <cmath>
#include <iostream>
#include <random>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << "FAIL: " << message << "\n";
    return false;
}

solver::SolveRequest simpleRequest(int num_nodes, int num_vehicles = 1) {
    solver::SolveRequest req;
    req.mutable_depot()->set_id("depot");
    int n = num_nodes + 1;
    req.set_matrix_size(n);
    for (int from = 0; from < n; ++from) {
        for (int to = 0; to < n; ++to) {
            req.add_distances(from == to ? 0.0 : 1000.0);
            req.add_durations(from == to ? 0.0 : 10.0);
        }
    }
    for (int i = 0; i < num_nodes; ++i) {
        auto* node = req.add_nodes();
        node->set_id("N-" + std::to_string(i + 1));
        node->set_type("delivery");
        node->set_demand(3);
        node->set_linehaul_demand(3);
        node->set_service_time(5);
        node->set_tw_start(480);
        node->set_tw_end(600);
        node->set_priority(2);
    }
    for (int i = 0; i < num_vehicles; ++i) {
        auto* v = req.add_vehicles();
        v->set_id("V-" + std::to_string(i + 1));
        v->set_capacity(100);
        v->set_shift_start(470);
        v->set_shift_end(700);
    }
    return req;
}

} // namespace

int main() {
    // Test 1: fromConstruction → toConstruction round-trip
    {
        auto req = simpleRequest(3, 1);
        auto constructed = hfvrptwb::adaptiveConstruct(req, 550.0, 4.0003, false, 0);

        auto alns_sol = hfvrptwb::alns::fromConstruction(constructed, req, 550.0, 4.0003);
        auto back = hfvrptwb::alns::toConstruction(alns_sol);

        if (!expect(constructed.routes.size() == back.routes.size(),
                    "round-trip: route count should match")) return 1;
        if (!expect(constructed.drops.size() == back.drops.size(),
                    "round-trip: drop count should match")) return 1;

        // Verify computeObjective matches writeResponse objective logic
        double obj = hfvrptwb::alns::computeObjective(alns_sol.vehicles, (int)alns_sol.unrouted.size());
        double expected_obj = 0.0;
        for (const auto& r : back.routes) expected_obj += r.total_cost;
        expected_obj += 1000.0 * back.drops.size();
        if (!expect(std::abs(obj - expected_obj) < 0.01,
                    "computeObjective should match sum(costs) + 1000*drops")) return 1;
    }

    // Test 2: fromConstruction with unrouted nodes (infeasible)
    {
        auto req = simpleRequest(5, 1);
        req.mutable_vehicles(0)->set_capacity(1); // tiny capacity forces drops
        auto constructed = hfvrptwb::adaptiveConstruct(req, 550.0, 4.0003, false, 0);

        auto alns_sol = hfvrptwb::alns::fromConstruction(constructed, req, 550.0, 4.0003);

        if (!expect(!alns_sol.unrouted.empty() || constructed.drops.empty(),
                    "unrouted should reflect dropped nodes")) return 1;

        double obj = hfvrptwb::alns::computeObjective(alns_sol.vehicles, (int)alns_sol.unrouted.size());
        if (!expect(obj >= 1000.0 * alns_sol.unrouted.size(),
                    "objective should penalize unrouted nodes heavily")) return 1;
    }

    // Test 3: ALNSSolution default construction
    {
        hfvrptwb::alns::ALNSSolution empty;
        if (!expect(empty.vehicles.empty(), "default solution should have no vehicles")) return 1;
        if (!expect(empty.unrouted.empty(), "default solution should have no unrouted")) return 1;
        if (!expect(std::abs(empty.objective - 0.0) < 0.001,
                    "default solution objective should be 0")) return 1;
    }

    // Test 4: AdaptivePenalty
    {
        hfvrptwb::alns::AdaptivePenalty penalty;
        if (!expect(penalty.capacity() == 1000.0, "default capacity penalty should be 1000")) return 1;
        if (!expect(penalty.timeWindow() == 1000.0, "default TW penalty should be 1000")) return 1;
        if (!expect(penalty.overtime() == 500.0, "default overtime penalty should be 500")) return 1;

        // Low feasible ratio should increase penalties
        penalty.update(0.05); // well below 0.20 target, factor=1.2 => 1000*1.2=1200
        if (!expect(penalty.capacity() > 1000.0, "low feasible ratio should increase capacity penalty")) return 1;
        if (!expect(penalty.timeWindow() > 1000.0, "low feasible ratio should increase TW penalty")) return 1;

        // High feasible ratio should decrease penalties
        penalty.update(0.50); // well above 0.20 target, factor=0.85 => 1200*0.85=1020
        if (!expect(penalty.capacity() < 1200.0, "high feasible ratio should decrease penalties")) return 1;

        // Penalties clamped to [min_clamp, 1e6]; min_clamp decays to floor
        hfvrptwb::alns::AdaptivePenalty p2;
        for (int i = 0; i < 200; ++i) p2.update(0.0); // drive up
        if (!expect(p2.capacity() <= 1e6, "capacity penalty should be clamped to 1e6")) return 1;

        for (int i = 0; i < 200; ++i) p2.update(1.0); // drive down; clamp has decayed
        if (!expect(p2.capacity() >= 10.0, "capacity penalty should be clamped to floor")) return 1;
    }

    // Destroy operator tests
    {
        auto req = simpleRequest(10, 2);
        auto constructed = hfvrptwb::adaptiveConstruct(req, 550.0, 4.0003, false, 0);
        auto sol = hfvrptwb::alns::fromConstruction(constructed, req, 550.0, 4.0003);

        int unrouted_before = (int)sol.unrouted.size();
        std::mt19937 rng(42);

        hfvrptwb::alns::randomRemoval(sol, rng, req, 550.0, 4.0003, 3);
        if (!expect((int)sol.unrouted.size() == unrouted_before + 3,
                    "randomRemoval should remove exactly q=3 nodes")) return 1;

        // Verify removed nodes are tracked in unrouted
        int total_accounted = 0;
        for (const auto& vt : sol.vehicles)
            for (const auto& trip : vt.trips)
                total_accounted += (int)trip.nodes.size();
        total_accounted += (int)sol.unrouted.size();
        if (!expect(total_accounted == req.nodes_size(),
                    "all nodes accounted after removal")) return 1;
    }

    // Repair operator tests
    {
        auto req = simpleRequest(5, 2);
        auto constructed = hfvrptwb::adaptiveConstruct(req, 550.0, 4.0003, false, 0);
        auto sol = hfvrptwb::alns::fromConstruction(constructed, req, 550.0, 4.0003);

        int unrouted_before = (int)sol.unrouted.size();

        // Manually remove 2 nodes to test re-insertion
        if (!sol.vehicles.empty() && !sol.vehicles[0].trips.empty()) {
            auto& trip = sol.vehicles[0].trips[0];
            if (trip.nodes.size() >= 2) {
                int n1 = trip.nodes.back(); trip.nodes.pop_back();
                int n2 = trip.nodes.back(); trip.nodes.pop_back();
                // Re-evaluate trip after removal
                const auto& vehicle = req.vehicles(sol.vehicles[0].vehicle_index);
                hfvrptwb::RouteState init;
                init.vehicle_index = sol.vehicles[0].vehicle_index;
                auto eval = hfvrptwb::evaluateRouteState(req, vehicle, init, trip.nodes, 550.0, 4.0003);
                if (eval.feasible) trip = std::move(eval.next);
                sol.unrouted.push_back(n1);
                sol.unrouted.push_back(n2);
                sol.objective = hfvrptwb::alns::computeObjective(sol.vehicles, (int)sol.unrouted.size());
            }
        }

        int unrouted_after_manual = (int)sol.unrouted.size();
        if (unrouted_after_manual > unrouted_before) {
            hfvrptwb::alns::greedyRepair(sol, req, 550.0, 4.0003, 0);
            // Some nodes might remain unrouted if infeasible, that's ok
            if (!expect((int)sol.unrouted.size() <= unrouted_after_manual,
                        "greedyRepair should not increase unrouted count")) return 1;
        }
    }

    // ALNS framework test
    {
        auto req = simpleRequest(20, 3);
        auto constructed = hfvrptwb::adaptiveConstruct(req, 550.0, 4.0003, false, 0);

        hfvrptwb::alns::ALNSSolver solver;
        auto result = solver.solve(req, constructed, 550.0, 4.0003,
                                    std::chrono::milliseconds(500), 0, 42);

        double alns_obj = 0.0;
        for (const auto& r : result.routes) alns_obj += r.total_cost;
        alns_obj += 1000.0 * result.drops.size();

        double construction_obj = 0.0;
        for (const auto& r : constructed.routes) construction_obj += r.total_cost;
        construction_obj += 1000.0 * constructed.drops.size();

        if (!expect(alns_obj <= construction_obj + 1e-6,
                    "ALNS should not worsen objective")) return 1;

        // Determinism test
        auto result2 = solver.solve(req, constructed, 550.0, 4.0003,
                                     std::chrono::milliseconds(500), 0, 42);
        double obj2 = 0.0;
        for (const auto& r : result2.routes) obj2 += r.total_cost;
        obj2 += 1000.0 * result2.drops.size();

        if (!expect(std::abs(alns_obj - obj2) < 0.01,
                    "ALNS should be deterministic with fixed seed")) return 1;
    }

    std::cout << "All solution tests passed.\n";
    return 0;
}
