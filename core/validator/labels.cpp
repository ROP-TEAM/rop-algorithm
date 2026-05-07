#include "validator/labels.h"

#include <algorithm>

namespace hfvrptwb {

ForwardLabel extendForwardLabel(
    const ForwardLabel& label,
    const StopSpec& stop,
    int travel_min,
    double distance_m)
{
    ForwardLabel next = label;
    int arrival = label.departure_time > 0
        ? label.departure_time + travel_min
        : label.earliest_arrival + travel_min;
    arrival = std::max(arrival, stop.earliest);

    next.earliest_arrival = arrival;
    next.time_warp = std::max(0, arrival - stop.latest);
    next.feasible = next.time_warp == 0;
    next.departure_time = arrival + stop.service;
    next.load_linehaul = label.load_linehaul - stop.linehaul;
    next.load_backhaul = label.load_backhaul + stop.backhaul;
    next.distance_m = label.distance_m + distance_m;
    return next;
}

} // namespace hfvrptwb
