#pragma once
#include "routing.pb.h"

namespace feasibility {

// True if vehicle and node share at least one tag, or if either has no tags.
bool isTagCompatible(const solver::Vehicle& vehicle, const solver::Node& node);

} // namespace feasibility
