#include "tags.h"
#include <unordered_set>
#include <string>

namespace feasibility {

bool isTagCompatible(const solver::Vehicle& vehicle, const solver::Node& node) {
    if (vehicle.tags_size() == 0 || node.tags_size() == 0) return true;
    std::unordered_set<std::string> vehicle_tags(
        vehicle.tags().begin(), vehicle.tags().end());
    for (const auto& tag : node.tags()) {
        if (vehicle_tags.count(tag)) return true;
    }
    return false;
}

} // namespace feasibility
