#pragma once

namespace feasibility {

// True if last_depart_min does not exceed shift_end_min.
// shift_end_min == 0 means no shift end restriction.
bool isShiftWindowFeasible(int last_depart_min, int shift_end_min);

} // namespace feasibility
