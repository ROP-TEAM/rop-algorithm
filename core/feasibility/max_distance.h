#pragma once

namespace feasibility {

// True if route_distance_m is within max_distance_m. max_distance_m == 0 means unlimited.
bool isMaxDistanceFeasible(double route_distance_m, double max_distance_m);

} // namespace feasibility
