#include "solver_service.h"
#include <iostream>
#include <string>

namespace {

// ---- helpers ----------------------------------------------------------------

bool expect(bool ok, const std::string& msg) {
    if (ok) return true;
    std::cerr << "FAIL: " << msg << "\n";
    return false;
}

// Uniform 1 000 m / 10 min between every pair, 0 on diagonal.
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

solver::Vehicle* addVehicle(solver::SolveRequest& req,
                             const std::string& id,
                             int capacity,
                             int shift_start, int shift_end) {
    auto* v = req.add_vehicles();
    v->set_id(id);
    v->set_capacity(capacity);
    v->set_shift_start(shift_start);
    v->set_shift_end(shift_end);
    return v;
}

solver::Node* addOrder(solver::SolveRequest& req,
                       const std::string& id,
                       int demand,
                       int tw_start, int tw_end) {
    auto* n = req.add_nodes();
    n->set_id(id);
    n->set_type("delivery");
    n->set_demand(demand);
    n->set_linehaul_demand(demand);
    n->set_service_time(5);
    n->set_tw_start(tw_start);
    n->set_tw_end(tw_end);
    return n;
}

SolveConfig multiTripCfg() {
    SolveConfig cfg;
    cfg.enableMultiTrip = true;
    cfg.reloadMin = 0;
    return cfg;
}

solver::SolveResponse solve(const solver::SolveRequest& req,
                             SolveConfig cfg = {}) {
    return SolverV2(cfg).Solve(req);
}

// ---- tests ------------------------------------------------------------------

// T1: or_opt must not crash with single vehicle, two trips.
// Two orders exceed capacity in one trip → two trips → or_opt runs intra-vehicle.
bool t1_single_vehicle_multi_trip_no_crash() {
    auto req = makeReq(2);
    addVehicle(req, "V1", 6, 480, 720);
    addOrder(req, "A", 5, 480, 720);
    addOrder(req, "B", 5, 480, 720);
    auto resp = solve(req, multiTripCfg());
    return expect(resp.unassigned_size() == 0,
                  "T1: both orders must be assigned (single vehicle, two trips)");
}

// T2: or_opt must not crash when vehicle 1 has kInf cost (infeasible route)
// and vehicle 2 is empty — stops should migrate.
// Trigger: V1 has 3 stops needing 3 trips, V2 gets nothing initially.
// Or_opt sees kInf on V1 and tries to relocate everything.
bool t2_infeasible_source_vehicle_no_crash() {
    auto req = makeReq(3);
    addVehicle(req, "V1", 4, 480, 530);   // tight shift — hard to fit 3 trips
    addVehicle(req, "V2", 20, 480, 720);
    addOrder(req, "A", 4, 480, 720);
    addOrder(req, "B", 4, 480, 720);
    addOrder(req, "C", 4, 480, 720);
    auto resp = solve(req, multiTripCfg());
    // Just test no crash — unassigned count may vary by feasibility
    return expect(resp.status() == "OK" || resp.status() == "INFEASIBLE",
                  "T2: solver must return valid status, not crash");
}

// T3: or_opt should consolidate: two vehicles each with 1 stop →
// one vehicle with 2 stops (lower fixed cost).
bool t3_consolidate_two_vehicles_into_one() {
    auto req = makeReq(2);
    addVehicle(req, "V1", 10, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);
    addOrder(req, "A", 3, 480, 720);
    addOrder(req, "B", 3, 480, 720);
    auto resp = solve(req, multiTripCfg());
    if (!expect(resp.unassigned_size() == 0, "T3: all orders must be assigned")) return false;
    return expect(resp.routes_size() == 1,
                  "T3: or_opt should consolidate two stops into one vehicle");
}

// T4: or_opt intra-vehicle relocation must preserve all stops.
// Three stops, single vehicle, no capacity issue — or_opt reorders within route.
bool t4_intra_vehicle_stop_count_preserved() {
    auto req = makeReq(3);
    addVehicle(req, "V1", 30, 480, 720);
    addOrder(req, "A", 5, 480, 720);
    addOrder(req, "B", 5, 480, 720);
    addOrder(req, "C", 5, 480, 720);
    auto resp = solve(req, multiTripCfg());
    if (!expect(resp.unassigned_size() == 0, "T4: all stops must remain assigned")) return false;
    int total_stops = 0;
    for (const auto& r : resp.routes()) total_stops += r.stops_size();
    return expect(total_stops == 3, "T4: total stop count must stay at 3 after or_opt");
}

// T5: or_opt with multi-trip vehicle and extra idle vehicle —
// idle vehicle should be dropped (zero routes for it).
bool t5_idle_vehicle_dropped() {
    auto req = makeReq(2);
    addVehicle(req, "V1", 10, 480, 720);
    addVehicle(req, "V2", 10, 480, 720);   // V2 should get nothing
    addOrder(req, "A", 3, 480, 720);
    addOrder(req, "B", 3, 480, 720);
    auto resp = solve(req);   // single-trip mode
    if (!expect(resp.unassigned_size() == 0, "T5: all orders assigned")) return false;
    return expect(resp.routes_size() == 1, "T5: idle vehicle must not produce an empty route");
}

} // namespace

int main() {
    int failed = 0;
    if (!t1_single_vehicle_multi_trip_no_crash())  ++failed;
    if (!t2_infeasible_source_vehicle_no_crash())  ++failed;
    if (!t3_consolidate_two_vehicles_into_one())   ++failed;
    if (!t4_intra_vehicle_stop_count_preserved())  ++failed;
    if (!t5_idle_vehicle_dropped())                ++failed;

    if (failed == 0)
        std::cout << "All or_opt tests passed.\n";
    else
        std::cout << failed << " test(s) failed.\n";
    return failed > 0 ? 1 : 0;
}
