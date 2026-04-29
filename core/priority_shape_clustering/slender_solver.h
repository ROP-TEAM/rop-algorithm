#ifndef SLENDER_SOLVER_H
#define SLENDER_SOLVER_H

#include <vector>
#include "types.h"

class SlenderSolver {
  public:
    static std::vector<Cluster> runOneRound(
        const std::vector<Node>& nodes,
        const std::vector<std::vector<double>>& delta,
        const std::vector<double>& active_capacities,
        const std::vector<int>& active_remaining_orders,
        std::vector<int>& unassigned_nodes
        );

    static std::vector<Cluster> clusterWithCenters(
        const std::vector<Node>& candidate_nodes,
        const std::vector<Node>& all_nodes,
        const std::vector<int>& centers,
        const std::vector<std::vector<double>>& distMatrix,
        const std::vector<double>& active_capacities,
        const std::vector<int>& active_remaining_orders
        );

  private:
    static std::vector<Cluster> kMedoidsIterate(
        const std::vector<Node>& candidate_nodes,
        std::vector<int>& centers,
        const std::vector<std::vector<double>>& delta,
        const std::vector<double>& active_capacities,
        const std::vector<int>& active_remaining_orders
        );
};

#endif
