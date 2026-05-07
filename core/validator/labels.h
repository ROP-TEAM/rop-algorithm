#pragma once

namespace hfvrptwb {

struct StopSpec {
    int earliest = 0;
    int latest = 1440;
    int service = 0;
    int linehaul = 0;
    int backhaul = 0;
};

struct ForwardLabel {
    bool feasible = true;
    int earliest_arrival = 0;
    int latest_arrival = 1440;
    int departure_time = 0;
    int load_linehaul = 0;
    int load_backhaul = 0;
    int time_warp = 0;
    double distance_m = 0.0;
};

struct BackwardLabel {
    bool feasible = true;
    int latest_departure = 1440;
    int slack = 0;
    int load_linehaul = 0;
    int load_backhaul = 0;
    int time_warp = 0;
    double distance_m = 0.0;
};

ForwardLabel extendForwardLabel(
    const ForwardLabel& label,
    const StopSpec& stop,
    int travel_min,
    double distance_m);

} // namespace hfvrptwb
