#include "capacity.h"

namespace feasibility {

bool fitsCapacity(double cluster_weight, double node_weight, double vehicle_capacity) {
    return cluster_weight + node_weight <= vehicle_capacity;
}

bool fitsMaxTasks(int cluster_size, int max_tasks) {
    return max_tasks == 0 || cluster_size < max_tasks;
}

} // namespace feasibility
