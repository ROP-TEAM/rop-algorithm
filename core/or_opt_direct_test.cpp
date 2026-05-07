// Direct unit tests for orOptRelocate — calls the function with hand-built
// ConstructionResult so the crash scenario can be isolated without going
// through the full construction/2-opt pipeline.
#include "routeOpt/or_opt.h"
#include "construction/adaptive_constructor.h"
#include "solver_service.h"
#include <iostream>
#include <string>
#include <cassert>

namespace {

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Uniform matrix: 1000 m / 10 min between every pair, 0 on diagonal.
solver::SolveRequest makeReq(int node_count) {
    solver::SolveRequest req;
    req.mutable_depot()->set_id("depot");
    int n = node_count + 1;
    req.set_matrix_size(n);
    for (int i = 0; i < n; ++i)
        for (int j = 0; j < n; ++j) {
            req.add_distances(i == j ? 0.0 : 1000.0);
            req.add_durations(i == j ? 0.0 : 10.0);
        }
    return req;
}

void addVehicle(solver::SolveRequest& req, const std::string& id,
                int cap, int shift_start, int shift_end) {
    auto* v = req.add_vehicles();
    v->set_id(id);
    v->set_capacity(cap);
    v->set_shift_start(shift_start);
    v->set_shift_end(shift_end);
}

void addNode(solver::SolveRequest& req, const std::string& id,
             int demand, int tw_start, int tw_end) {
    auto* n = req.add_nodes();
    n->set_id(id);
    n->set_type("delivery");
    n->set_demand(demand);
    n->set_linehaul_demand(demand);
    n->set_service_time(5);
    n->set_tw_start(tw_start);
    n->set_tw_end(tw_end);
}

hfvrptwb::ConstructedRoute makeRoute(int v_idx,
                                     std::vector<std::vector<int>> trips) {
    hfvrptwb::ConstructedRoute r;
    r.vehicle_index = v_idx;
    r.trips = trips;
    for (const auto& t : trips)
        r.nodes.insert(r.nodes.end(), t.begin(), t.end());
    r.total_cost = 0.0;
    return r;
}

SolveConfig multiCfg() {
    SolveConfig cfg;
    cfg.enableMultiTrip = true;
    cfg.reloadMin = 0;
    return cfg;
}

bool run(const std::string& name, hfvrptwb::ConstructionResult plan,
         const solver::SolveRequest& req, const SolveConfig& cfg) {
    std::cout << "  running: " << name << " ... " << std::flush;
    try {
        hfvrptwb::orOptRelocate(plan, req, cfg);
        std::cout << "OK\n";
        return true;
    } catch (...) {
        std::cout << "EXCEPTION\n";
        return false;
    }
    // assertion crashes terminate the process — the label above shows which test
}

// ---------------------------------------------------------------------------
// Test scenarios
// ---------------------------------------------------------------------------

// S1: two vehicles, each with ONE single-stop trip.
// orOptRelocate should try to merge them (cross-vehicle move).
// Exercises: src_after = empty, bi != ai, trip_slots = base.size()+1.
bool s1_two_vehicles_one_stop_each() {
    auto req = makeReq(2);
    addVehicle(req, "V1", 10, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);
    addNode(req, "A", 3, 480, 720);
    addNode(req, "B", 3, 480, 720);

    hfvrptwb::ConstructionResult plan;
    plan.routes.push_back(makeRoute(0, {{1}}));
    plan.routes.push_back(makeRoute(1, {{2}}));

    return run("S1: 2 vehicles 1-stop each", std::move(plan), req, multiCfg());
}

// S2: one vehicle with TWO single-stop trips.
// Exercises: same-vehicle relocation, sameVehicleTripSurvived=false (trip removed).
bool s2_one_vehicle_two_single_stop_trips() {
    auto req = makeReq(2);
    addVehicle(req, "V1", 4, 480, 720);
    addNode(req, "A", 4, 480, 720);
    addNode(req, "B", 4, 480, 720);

    hfvrptwb::ConstructionResult plan;
    plan.routes.push_back(makeRoute(0, {{1}, {2}}));

    return run("S2: 1 vehicle 2 single-stop trips", std::move(plan), req, multiCfg());
}

// S3: one vehicle with one TWO-stop trip.
// Exercises: same-vehicle relocation, sameVehicleTripSurvived=true.
bool s3_one_vehicle_one_two_stop_trip() {
    auto req = makeReq(2);
    addVehicle(req, "V1", 10, 480, 720);
    addNode(req, "A", 3, 480, 720);
    addNode(req, "B", 3, 480, 720);

    hfvrptwb::ConstructionResult plan;
    plan.routes.push_back(makeRoute(0, {{1, 2}}));

    return run("S3: 1 vehicle 1 two-stop trip", std::move(plan), req, multiCfg());
}

// S4: two vehicles — V1 has a two-stop trip, V2 has a one-stop trip.
// Exercises: cross-vehicle move with non-trivial base_trips.
bool s4_two_vehicles_two_plus_one() {
    auto req = makeReq(3);
    addVehicle(req, "V1", 10, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);
    addNode(req, "A", 3, 480, 720);
    addNode(req, "B", 3, 480, 720);
    addNode(req, "C", 3, 480, 720);

    hfvrptwb::ConstructionResult plan;
    plan.routes.push_back(makeRoute(0, {{1, 2}}));
    plan.routes.push_back(makeRoute(1, {{3}}));

    return run("S4: V1 2-stop, V2 1-stop", std::move(plan), req, multiCfg());
}

// S5: two vehicles — V1 has TWO trips (multi-trip), V2 is empty (no route).
// Exercises: ai has multi-trip, bi vehicle not in routes at all.
bool s5_one_vehicle_two_trips_no_second_vehicle() {
    auto req = makeReq(2);
    addVehicle(req, "V1", 4, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);
    addNode(req, "A", 4, 480, 720);
    addNode(req, "B", 4, 480, 720);

    hfvrptwb::ConstructionResult plan;
    // V2 has no route — only V1's two single-stop trips appear.
    plan.routes.push_back(makeRoute(0, {{1}, {2}}));

    return run("S5: V1 two single-stop trips, V2 absent", std::move(plan), req, multiCfg());
}

// S6: three vehicles, each with one stop.
// Exercises: ai loops through multiple sources, bi tries each destination.
bool s6_three_vehicles_one_stop_each() {
    auto req = makeReq(3);
    addVehicle(req, "V1", 10, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);
    addVehicle(req, "V3", 10, 480, 720);
    addNode(req, "A", 3, 480, 720);
    addNode(req, "B", 3, 480, 720);
    addNode(req, "C", 3, 480, 720);

    hfvrptwb::ConstructionResult plan;
    plan.routes.push_back(makeRoute(0, {{1}}));
    plan.routes.push_back(makeRoute(1, {{2}}));
    plan.routes.push_back(makeRoute(2, {{3}}));

    return run("S6: 3 vehicles 1-stop each", std::move(plan), req, multiCfg());
}

// S7: one vehicle with THREE single-stop trips.
// Exercises: ti iterates across 3 trips, same-vehicle, trip-removed case.
bool s7_one_vehicle_three_single_stop_trips() {
    auto req = makeReq(3);
    addVehicle(req, "V1", 4, 480, 720);
    addNode(req, "A", 4, 480, 720);
    addNode(req, "B", 4, 480, 720);
    addNode(req, "C", 4, 480, 720);

    hfvrptwb::ConstructionResult plan;
    plan.routes.push_back(makeRoute(0, {{1}, {2}, {3}}));

    return run("S7: 1 vehicle 3 single-stop trips", std::move(plan), req, multiCfg());
}

// S8: two vehicles — V1 has TWO multi-stop trips, V2 has one multi-stop trip.
// This is the most complex relocation scenario.
bool s8_two_vehicles_multi_stop_multi_trip() {
    auto req = makeReq(5);
    addVehicle(req, "V1", 4, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);
    addNode(req, "A", 4, 480, 720);
    addNode(req, "B", 4, 480, 720);
    addNode(req, "C", 3, 480, 720);
    addNode(req, "D", 3, 480, 720);
    addNode(req, "E", 3, 480, 720);

    hfvrptwb::ConstructionResult plan;
    plan.routes.push_back(makeRoute(0, {{1}, {2}}));        // V1: A, B (single-stop trips)
    plan.routes.push_back(makeRoute(1, {{3, 4, 5}}));       // V2: C, D, E (one trip)

    return run("S8: V1 2 single-stop, V2 3-stop trip", std::move(plan), req, multiCfg());
}

// S9: same as S8 but single-trip mode (enableMultiTrip=false).
// Exercises: no extra trip slot allowed.
bool s9_single_trip_mode() {
    auto req = makeReq(5);
    addVehicle(req, "V1", 10, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);
    addNode(req, "A", 3, 480, 720);
    addNode(req, "B", 3, 480, 720);
    addNode(req, "C", 3, 480, 720);
    addNode(req, "D", 3, 480, 720);
    addNode(req, "E", 3, 480, 720);

    hfvrptwb::ConstructionResult plan;
    plan.routes.push_back(makeRoute(0, {{1, 2}}));
    plan.routes.push_back(makeRoute(1, {{3, 4, 5}}));

    SolveConfig cfg;  // enableMultiTrip = false by default
    return run("S9: single-trip mode 2-stop vs 3-stop", std::move(plan), req, cfg);
}

// S10: route with an empty trips vector (single-trip path via r.nodes).
// Exercises: nr.trips = {r.nodes} branch.
bool s10_route_without_trips_field() {
    auto req = makeReq(2);
    addVehicle(req, "V1", 10, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);
    addNode(req, "A", 3, 480, 720);
    addNode(req, "B", 3, 480, 720);

    hfvrptwb::ConstructionResult plan;
    // Set nodes directly, leave trips empty (simulates single-trip construction output)
    hfvrptwb::ConstructedRoute r1;
    r1.vehicle_index = 0;
    r1.nodes = {1};
    // r1.trips intentionally empty
    plan.routes.push_back(r1);

    hfvrptwb::ConstructedRoute r2;
    r2.vehicle_index = 1;
    r2.nodes = {2};
    plan.routes.push_back(r2);

    return run("S10: routes without trips field (single-trip path)", std::move(plan), req, multiCfg());
}

} // namespace

int main() {
    std::cout << "=== orOptRelocate direct crash isolation ===\n";
    int failed = 0;
    if (!s1_two_vehicles_one_stop_each())         ++failed;
    if (!s2_one_vehicle_two_single_stop_trips())  ++failed;
    if (!s3_one_vehicle_one_two_stop_trip())      ++failed;
    if (!s4_two_vehicles_two_plus_one())          ++failed;
    if (!s5_one_vehicle_two_trips_no_second_vehicle()) ++failed;
    if (!s6_three_vehicles_one_stop_each())       ++failed;
    if (!s7_one_vehicle_three_single_stop_trips()) ++failed;
    if (!s8_two_vehicles_multi_stop_multi_trip()) ++failed;
    if (!s9_single_trip_mode())                   ++failed;
    if (!s10_route_without_trips_field())         ++failed;

    if (failed == 0)
        std::cout << "All direct tests passed — crash not reproduced here.\n";
    else
        std::cout << failed << " test(s) crashed or threw.\n";
    return failed > 0 ? 1 : 0;
}
