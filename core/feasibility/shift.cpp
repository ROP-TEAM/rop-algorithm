#include "shift.h"

namespace feasibility {

bool isShiftWindowFeasible(int last_depart_min, int shift_end_min) {
    return shift_end_min == 0 || last_depart_min <= shift_end_min;
}

} // namespace feasibility
