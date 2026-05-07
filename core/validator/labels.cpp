#include "validator/labels.h"

#include <algorithm>
#include <sstream>

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

RouteLabelSummary summarizeForwardLabels(
    const std::vector<ForwardLabel>& labels,
    int vehicle_capacity,
    int shift_end,
    double total_distance_m)
{
    RouteLabelSummary summary;
    summary.total_distance_m = total_distance_m;
    if (labels.empty()) return summary;

    for (const auto& label : labels) {
        summary.time_warp += label.time_warp;
        summary.max_load = std::max(summary.max_load, label.load_linehaul + label.load_backhaul);
        if (!label.feasible && summary.fail_code.empty()) {
            summary.feasible = false;
            summary.fail_code = "TIME_WINDOW";
            summary.fail_detail = "route has time-window warp";
        }
    }

    const auto& last = labels.back();
    summary.last_departure = last.departure_time;
    if (summary.max_load > vehicle_capacity) {
        summary.feasible = false;
        summary.fail_code = "CAPACITY";
        std::ostringstream msg;
        msg << "max load " << summary.max_load << " > capacity " << vehicle_capacity;
        summary.fail_detail = msg.str();
        return summary;
    }
    if (shift_end > 0 && summary.last_departure > shift_end) {
        summary.feasible = false;
        summary.fail_code = "SHIFT";
        std::ostringstream msg;
        msg << "last departure " << summary.last_departure << " > shift_end " << shift_end;
        summary.fail_detail = msg.str();
        return summary;
    }
    return summary;
}

} // namespace hfvrptwb
