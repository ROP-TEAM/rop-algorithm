#include "solver_service.h"

#include <iostream>
#include <string>

namespace {

solver::SolveRequest baseRequest(int node_count) {
    solver::SolveRequest req;
    req.mutable_depot()->set_id("depot");
    req.set_matrix_size(node_count + 1);

    int n = req.matrix_size();
    for (int from = 0; from < n; ++from) {
        for (int to = 0; to < n; ++to) {
            req.add_distances(from == to ? 0.0 : 1000.0);
            req.add_durations(from == to ? 0.0 : 10.0);
        }
    }

    auto* vehicle = req.add_vehicles();
    vehicle->set_id("V-1");
    vehicle->set_capacity(10);
    vehicle->set_shift_start(480);
    vehicle->set_shift_end(720);
    return req;
}

solver::Node* addDelivery(solver::SolveRequest& req, const std::string& id, int demand = 5) {
    auto* node = req.add_nodes();
    node->set_id(id);
    node->set_type("delivery");
    node->set_demand(demand);
    node->set_linehaul_demand(demand);
    node->set_service_time(5);
    node->set_tw_start(480);
    node->set_tw_end(720);
    return node;
}

solver::Node* addPickup(solver::SolveRequest& req, const std::string& id, int demand = 5) {
    auto* node = req.add_nodes();
    node->set_id(id);
    node->set_type("pickup");
    node->set_demand(demand);
    node->set_backhaul_demand(demand);
    node->set_service_time(5);
    node->set_tw_start(480);
    node->set_tw_end(720);
    return node;
}

solver::SolveResponse solve(const solver::SolveRequest& req) {
    SolverV2 solver;
    return solver.Solve(req);
}

bool expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << message << "\n";
    return false;
}

bool hasDropCode(const solver::SolveResponse& resp, const std::string& code) {
    for (const auto& reason : resp.drop_reasons()) {
        if (reason.code() == code) return true;
    }
    return false;
}

} // namespace

int main() {
    {
        auto req = baseRequest(1);
        addDelivery(req, "ORD-TAG")->add_tags("fragile");
        req.mutable_vehicles(0)->add_tags("heavy");
        auto resp = solve(req);
        if (!expect(resp.unassigned_size() == 1, "tag mismatch should leave order unassigned")) return 1;
        if (!expect(hasDropCode(resp, "TAG_MISMATCH"), "tag mismatch should use TAG_MISMATCH")) return 1;
    }

    {
        auto req = baseRequest(1);
        addDelivery(req, "ORD-CAP", 11);
        auto resp = solve(req);
        if (!expect(resp.unassigned_size() == 1, "capacity failure should leave order unassigned")) return 1;
        if (!expect(hasDropCode(resp, "CAPACITY_FULL"), "capacity failure should use CAPACITY_FULL")) return 1;
    }

    {
        auto req = baseRequest(1);
        auto* node = addDelivery(req, "ORD-TW");
        node->set_tw_end(485);
        auto resp = solve(req);
        if (!expect(resp.unassigned_size() == 1, "time window failure should leave order unassigned")) return 1;
        if (!expect(hasDropCode(resp, "TIMEWINDOW_TIGHT"), "time window failure should use TIMEWINDOW_TIGHT")) return 1;
    }

    {
        auto req = baseRequest(1);
        addDelivery(req, "ORD-DIST");
        req.mutable_vehicles(0)->set_max_distance(1000.0);
        auto resp = solve(req);
        if (!expect(resp.unassigned_size() == 1, "max distance failure should leave order unassigned")) return 1;
        if (!expect(hasDropCode(resp, "MAX_DISTANCE"), "max distance failure should use MAX_DISTANCE")) return 1;
    }

    {
        auto req = baseRequest(1);
        addDelivery(req, "ORD-NO-VEHICLE");
        req.clear_vehicles();
        auto resp = solve(req);
        if (!expect(resp.unassigned_size() == 1, "no vehicle should leave order unassigned")) return 1;
        if (!expect(hasDropCode(resp, "NO_VEHICLE"), "no vehicle should use NO_VEHICLE")) return 1;
    }

    {
        auto req = baseRequest(2);
        auto* pickup = addPickup(req, "PICKUP");
        pickup->set_pair_id("PAIR-1");
        auto* delivery = addDelivery(req, "DELIVERY");
        delivery->set_pair_id("PAIR-1");
        auto resp = solve(req);
        if (!expect(resp.routes_size() == 1, "paired order should be routed")) return 1;
        if (!expect(resp.routes(0).stops_size() == 2, "paired order should have two stops")) return 1;
        if (!expect(resp.routes(0).stops(0).node_id() == "PICKUP", "pickup must be before delivery")) return 1;
        if (!expect(resp.routes(0).stops(1).node_id() == "DELIVERY", "delivery must follow pickup")) return 1;
    }

    return 0;
}
