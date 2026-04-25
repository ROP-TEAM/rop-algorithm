#ifndef SLENDER_SOLVER_H
#define SLENDER_SOLVER_H

#include <vector>
#include "types.h"

class SlenderSolver {
public:
    static std::vector<Cluster> run(int N, int K, double capacity, 
                                    const std::vector<Node>& nodes, 
                                    const std::vector<std::vector<double>>& delta);

private:
    static void assign(int N, int K, double capacity, 
                       const std::vector<Node>& nodes, 
                       const std::vector<int>& centers, 
                       const std::vector<std::vector<double>>& delta, 
                       std::vector<Cluster>& clusters);

    static bool update(int K, std::vector<int>& centers, 
                       const std::vector<Cluster>& clusters, 
                       const std::vector<std::vector<double>>& delta);
};

#endif
