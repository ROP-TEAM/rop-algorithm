#ifndef SLENDER_SOLVER_H
#define SLENDER_SOLVER_H

#include <vector>
#include "types.h"

// Time-aware cluster-first VRP solver using Macro Node theory.
//
// Each call to runOneRound performs one K-medoids pass over the unassigned
// order nodes.  It respects hard time windows via incremental Macro Node
// merge rules and vehicle capacity per trip.  Multiple trips per vehicle are
// obtained by calling runOneRound repeatedly until all orders are assigned.
class SlenderSolver {
public:
    // Assigns unassigned_nodes to K clusters (one per vehicle in the subset)
    // using time-aware K-medoids with multi-restart.
    //
    // all_nodes  : full node array; index 0 = depot, 1..N-1 = orders.
    // delta      : N×N slender similarity matrix (from computeSlenderMatrix).
    // dur        : N×N travel-duration matrix in minutes.
    // active_capacities : per-vehicle weight capacity for this trip (size K).
    // unassigned_nodes  : order indices to cluster; assigned ones are removed.
    //
    // Returns K Cluster objects (one per vehicle).  Empty cluster = vehicle
    // received no orders this round.  node_ids is in time-feasible visit order.
    static std::vector<Cluster> runOneRound(
        const std::vector<Node>&                all_nodes,
        const std::vector<std::vector<double>>& delta,
        const std::vector<std::vector<double>>& dur,
        const std::vector<double>&              active_capacities,
        std::vector<int>&                       unassigned_nodes,
        const std::vector<double>&              vehicle_ready_times
    );
};

#endif
