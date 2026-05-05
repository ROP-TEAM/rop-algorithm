#pragma once

namespace feasibility {

// True if adding node_weight would not exceed vehicle_capacity.
bool fitsCapacity(double cluster_weight, double node_weight, double vehicle_capacity);

// True if adding one task is within max_tasks limit. max_tasks == 0 means unlimited.
bool fitsMaxTasks(int cluster_size, int max_tasks);

} // namespace feasibility
