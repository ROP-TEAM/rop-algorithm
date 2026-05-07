#include "validator/labels.h"

#include <iostream>
#include <string>

namespace {

bool expect(bool condition, const std::string& message) {
    if (condition) return true;
    std::cerr << message << "\n";
    return false;
}

} // namespace

int main() {
    hfvrptwb::ForwardLabel depot;
    depot.earliest_arrival = 480;
    depot.latest_arrival = 720;
    depot.load_linehaul = 5;

    hfvrptwb::StopSpec delivery;
    delivery.earliest = 500;
    delivery.latest = 540;
    delivery.service = 10;
    delivery.linehaul = 3;

    auto next = hfvrptwb::extendForwardLabel(depot, delivery, 15, 1000.0);
    if (!expect(next.feasible, "delivery label should be feasible")) return 1;
    if (!expect(next.earliest_arrival == 500, "early arrival should wait to earliest")) return 1;
    if (!expect(next.departure_time == 510, "departure should include service")) return 1;
    if (!expect(next.load_linehaul == 2, "linehaul load should decrease after delivery")) return 1;
    if (!expect(next.distance_m == 1000.0, "distance should accumulate")) return 1;
    if (!expect(next.time_warp == 0, "feasible label should have zero time warp")) return 1;

    hfvrptwb::StopSpec late = delivery;
    late.latest = 495;
    auto late_next = hfvrptwb::extendForwardLabel(depot, late, 15, 1000.0);
    if (!expect(!late_next.feasible, "late arrival should be infeasible in hard mode")) return 1;
    if (!expect(late_next.time_warp == 5, "late arrival should record time warp")) return 1;

    hfvrptwb::StopSpec pickup;
    pickup.earliest = 480;
    pickup.latest = 720;
    pickup.service = 5;
    pickup.backhaul = 4;
    auto pickup_next = hfvrptwb::extendForwardLabel(depot, pickup, 10, 500.0);
    if (!expect(pickup_next.load_backhaul == 4, "backhaul load should increase after pickup")) return 1;

    return 0;
}
