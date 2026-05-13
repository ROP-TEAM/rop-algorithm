#ifndef SLENDER_SOLVER_H
#define SLENDER_SOLVER_H

#include <vector>
#include "types.h"

// Per-vehicle multi-trip result from SlenderSolver::plan().
struct SlenderPlan {
    // trips_per_vehicle[v][trip] = Cluster for vehicle v on trip index.
    std::vector<std::vector<Cluster>> trips_per_vehicle;
    // Order node indices (1-based) that could not be assigned.
    std::vector<int> unassigned;
};

// Time-aware cluster-first VRP solver using Macro Node theory.
class SlenderSolver {
public:
    // High-level entry point: build slender matrix then run multi-trip
    // K-medoids until all orders are assigned or no progress is made.
    //
    // nodes   : full node array; index 0 = depot, 1..N-1 = orders.
    // vehicles: fleet to assign — all vehicles are used (K = vehicles.size()).
    // dur     : N×N travel-duration matrix in minutes (row-major, depot=0).
    static SlenderPlan plan(
        const std::vector<Node>&                nodes,
        const std::vector<Vehicle>&             vehicles,
        const std::vector<std::vector<double>>& dur
    );

    // Low-level: one K-medoids pass over unassigned_nodes using a
    // pre-built delta matrix. Removes assigned indices from unassigned_nodes.
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
