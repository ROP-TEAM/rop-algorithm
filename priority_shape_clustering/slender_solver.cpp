#include "slender_solver.h"
#include <algorithm>
#include <random>
#include <iostream>

std::vector<Cluster> SlenderSolver::runOneRound(
    const std::vector<Node>& nodes,
    const std::vector<std::vector<double>>& delta,
    const std::vector<double>& active_capacities,
    const std::vector<int>& active_remaining_orders,
    std::vector<int>& unassigned_nodes) 
{
    int K = active_capacities.size();
    if (K == 0 || unassigned_nodes.empty()) return {};

    std::vector<Cluster> clusters(K);
    std::mt19937 rng(std::random_device{}());
    
    // 1. random k-centers from unassigned nodes
    std::vector<int> pool = unassigned_nodes;
    std::shuffle(pool.begin(), pool.end(), rng);
    std::vector<int> centers(K);
    
    for (int k = 0; k < K; ++k) {
        centers[k] = pool[k % pool.size()]; 
    }

    // 2. K-Medoids Loop
    bool changed = true;
    int iter = 0;
    while (changed && iter < 20) {
        // Reset Assignment
        for (int k = 0; k < K; ++k) {
            clusters[k].node_ids.clear();
            clusters[k].total_weight = 0.0;
            clusters[k].center_id = centers[k];
        }

        // Assign Nodes
        for (int node_idx : unassigned_nodes) {
            double min_score = 1e18;
            int best_k = -1;

            for (int k = 0; k < K; ++k) {
                // check capacity and remaining orders
                if (clusters[k].total_weight + nodes[node_idx].weight <= active_capacities[k] &&
                    clusters[k].node_ids.size() < active_remaining_orders[k]) {
                    
                    double score = delta[node_idx][centers[k]];
                    if (score < min_score) {
                        min_score = score;
                        best_k = k;
                    }
                }
            }

            if (best_k != -1) {
                clusters[best_k].node_ids.push_back(node_idx);
                clusters[best_k].total_weight += nodes[node_idx].weight;
            }
        }

        // Update Centers
        changed = false;
        for (int k = 0; k < K; ++k) {
            if (clusters[k].node_ids.empty()) continue;
            int best_c = centers[k];
            double min_sum = 1e18;
            for (int cand : clusters[k].node_ids) {
                double sum = 0;
                for (int mem : clusters[k].node_ids) sum += delta[cand][mem];
                if (sum < min_sum) { min_sum = sum; best_c = cand; }
            }
            if (best_c != centers[k]) { centers[k] = best_c; changed = true; }
        }
        iter++;
    }

    // 3. remove assigned nodes from unassigned nodes
    std::vector<int> new_unassigned;
    for (int node : unassigned_nodes) {
        bool found = false;
        for (const auto& c : clusters) {
            if (std::find(c.node_ids.begin(), c.node_ids.end(), node) != c.node_ids.end()) {
                found = true; break;
            }
        }
        if (!found) new_unassigned.push_back(node);
    }
    unassigned_nodes = new_unassigned; // update unassigned nodes

    return clusters;
}
