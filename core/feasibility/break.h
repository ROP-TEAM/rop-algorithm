#pragma once
#include <utility>
#include <vector>

namespace feasibility {

// True if at least one travel gap overlaps with [break_start, break_end].
// Returns true immediately if break_start_min == 0 (no break required).
// A gap is (depart_of_prev_stop, arrival_at_next_stop) in minutes from midnight.
bool isBreakWindowFeasible(
    const std::vector<std::pair<int, int>>& travel_gaps,
    int break_start_min,
    int break_end_min);

} // namespace feasibility
