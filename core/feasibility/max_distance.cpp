#include "max_distance.h"

namespace feasibility {

bool isMaxDistanceFeasible(double route_distance_m, double max_distance_m) {
    return max_distance_m == 0.0 || route_distance_m <= max_distance_m;
}

} // namespace feasibility
