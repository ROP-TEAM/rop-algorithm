#ifndef SLENDER_SOLVER_H
#define SLENDER_SOLVER_H

#include <vector>
#include "types.h"

class SlenderSolver {
  public:
    // static std::vector<Cluster> run(int N, int K, double capacity, 
    //     const std::vector<Node>& nodes, 
    //     const std::vector<std::vector<double>>& delta);

    // static std::vector<Cluster> runOneRound(
    //     int N,
    //     int K_active,
    //     const std::vector<Node>& nodes,
    //     const std::vector<std::vector<double>>& delta,
    //     const std::vector<int>& active_vehicle_indices,
    //     const std::vector<double>& capacities_per_trip,
    //     const std::vector<int>& remaining_orders,
    //     std::vector<int>& unassigned_nodes
    //     );
    static std::vector<Cluster> runOneRound(
        const std::vector<Node>& nodes,
        const std::vector<std::vector<double>>& delta,
        const std::vector<double>& active_capacities,
        const std::vector<int>& active_remaining_orders,
        std::vector<int>& unassigned_nodes 
        );

  private:
    static void assign(int N, int K, double capacity, 
        const std::vector<Node>& nodes, 
        const std::vector<int>& centers, 
        const std::vector<std::vector<double>>& delta, 
        std::vector<Cluster>& clusters);

    static bool update(int K, std::vector<int>& centers, 
        const std::vector<Cluster>& clusters, 
        const std::vector<std::vector<double>>& delta);

    static void assignWithConstraints(
        int N,
        int K_active,
        const std::vector<Node>& nodes,
        const std::vector<int>& centers,
        const std::vector<std::vector<double>>& delta,
        const std::vector<double>& capacities_per_trip,
        const std::vector<int>& remaining_orders,
        const std::vector<int>& active_vehicle_indices,
        std::vector<Cluster>& clusters,
        std::vector<int>& unassigned_nodes   // delete assign out
        );

};

#endif
