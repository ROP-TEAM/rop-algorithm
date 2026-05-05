#include "break.h"

namespace feasibility {

bool isBreakWindowFeasible(
    const std::vector<std::pair<int, int>>& travel_gaps,
    int break_start_min,
    int break_end_min)
{
    if (break_start_min == 0) return true;
    for (const auto& [gap_start, gap_end] : travel_gaps) {
        if (gap_start < break_end_min && break_start_min < gap_end) return true;
    }
    return false;
}

} // namespace feasibility
