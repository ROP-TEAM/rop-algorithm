#include "validator/route_state.h"

#include <cmath>
#include <iostream>
#include <string>

namespace {

solver::SolveRequest buildRequest(int vehicle_capacity = 10) {
    solver::SolveRequest req;
    req.mutable_depot()->set_id("depot");
    req.set_matrix_size(2);
    req.add_distances(0.0);
    req.add_distances(1000.0);
    req.add_distances(1000.0);
    req.add_distances(0.0);
    req.add_durations(0.0);
    req.add_durations(10.0);
    req.add_durations(10.0);
    req.add_durations(0.0);

    auto* node = req.add_nodes();
    node->set_id("ORD-1");
    node->set_type("delivery");
    node->set_demand(5);
    node->set_linehaul_demand(5);
    node->set_service_time(5);
    node->set_tw_start(480);
    node->set_tw_end(600);

    auto* vehicle = req.add_vehicles();
    vehicle->set_id("V-1");
    vehicle->set_capacity(vehicle_capacity);
    vehicle->set_shift_start(470);
    vehicle->set_shift_end(700);
    return req;
}

bool expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << message << "\n";
    return false;
}

} // namespace

int main() {
    {
        auto req = buildRequest();
        hfvrptwb::RouteState route;
        route.vehicle_index = 0;

        auto eval = hfvrptwb::evaluateInsertion(
            req, req.vehicles(0), route, 1, 0, 550.0, 4.0003);

        if (!expect(eval.feasible, "single delivery should be insertable")) return 1;
        if (!expect(eval.next.nodes.size() == 1, "route should contain one node")) return 1;
        if (!expect(eval.next.nodes[0] == 1, "route should contain inserted node")) return 1;
        if (!expect(eval.next.duration_min == 25, "route duration should include wait, service, return")) return 1;
        if (!expect(std::abs(eval.next.cost - (550.0 + 2.0 * 4.0003)) < 0.01,
                    "route cost should use fixed + km cost")) return 1;
        if (!expect(eval.next.forward_labels.size() == 1, "route should store one forward label")) return 1;
        if (!expect(eval.next.forward_labels[0].earliest_arrival == 480, "label should store arrival time")) return 1;
        if (!expect(eval.next.forward_labels[0].load_linehaul == 0, "label should track delivered load")) return 1;
    }

    {
        auto req = buildRequest(4);
        hfvrptwb::RouteState route;
        route.vehicle_index = 0;

        auto eval = hfvrptwb::evaluateInsertion(
            req, req.vehicles(0), route, 1, 0, 550.0, 4.0003);

        if (!expect(!eval.feasible, "over-capacity delivery should be rejected")) return 1;
        if (!expect(eval.fail_code == "CAPACITY", "failure code should be CAPACITY")) return 1;
    }

    return 0;
}
