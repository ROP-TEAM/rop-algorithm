#include "drop/drop_logic.h"

namespace hfvrptwb {

std::string normalizeDropCode(const std::string& code) {
    if (code == "TAG") return "TAG_MISMATCH";
    if (code == "CAPACITY") return "CAPACITY_FULL";
    if (code == "TIME_WINDOW") return "TIMEWINDOW_TIGHT";
    if (code == "SHIFT") return "DRIVER_HOURS";
    if (code.empty()) return "NO_FEASIBLE_INSERTION";
    return code;
}

} // namespace hfvrptwb
