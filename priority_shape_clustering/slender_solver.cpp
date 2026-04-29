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

    std::vector<Cluster> best_clusters;
    double best_score = 1e18;
    std::mt19937 rng(std::random_device{}());
    
    // restart loop
    for (int run = 0; run < 20; ++run) {
        
        std::vector<Cluster> current_clusters(K);
        std::vector<int> pool = unassigned_nodes;
        std::shuffle(pool.begin(), pool.end(), rng);
        std::vector<int> centers(K);
        
        for (int k = 0; k < K; ++k) {
            centers[k] = pool[k % pool.size()]; 
        }

        // K-Medoids Loop (Assign -> Update)
        bool changed = true;
        int iter = 0;
        while (changed && iter < 20) {
            // 1. Reset
            for (int k = 0; k < K; ++k) {
                current_clusters[k].node_ids.clear();
                current_clusters[k].total_weight = 0.0;
                current_clusters[k].center_id = centers[k];
            }

            // 2. Assign
            for (int node_idx : unassigned_nodes) {
                double min_score = 1e18;
                int best_k = -1;
                for (int k = 0; k < K; ++k) {
                    if (current_clusters[k].total_weight + nodes[node_idx].weight <= active_capacities[k] &&
                        current_clusters[k].node_ids.size() < active_remaining_orders[k]) {
                        
                        double score = delta[node_idx][centers[k]];
                        if (score < min_score) {
                            min_score = score;
                            best_k = k;
                        }
                    }
                }
                if (best_k != -1) {
                    current_clusters[best_k].node_ids.push_back(node_idx);
                    current_clusters[best_k].total_weight += nodes[node_idx].weight;
                }
            }

            // 3. Update Centers
            changed = false;
            for (int k = 0; k < K; ++k) {
                if (current_clusters[k].node_ids.empty()) continue;
                int best_c = centers[k];
                double min_sum = 1e18;
                for (int cand : current_clusters[k].node_ids) {
                    double sum = 0;
                    for (int mem : current_clusters[k].node_ids) sum += delta[cand][mem];
                    if (sum < min_sum) { min_sum = sum; best_c = cand; }
                }
                if (best_c != centers[k]) { centers[k] = best_c; changed = true; }
            }
            iter++;
        }

        // check slender score
        double current_score = 0;
        int assigned_count = 0;
        for (const auto& c : current_clusters) {
            for (int id : c.node_ids) {
                current_score += delta[id][c.center_id];
                assigned_count++;
            }
        }

        // if better
        if (current_score < best_score && assigned_count > 0) {
            best_score = current_score;
            best_clusters = current_clusters;
        }
    }

    // whoever got they car delete them
    if (!best_clusters.empty()) {
        std::vector<int> new_unassigned;
        for (int node : unassigned_nodes) {
            bool found = false;
            for (const auto& c : best_clusters) {
                if (std::find(c.node_ids.begin(), c.node_ids.end(), node) != c.node_ids.end()) {
                    found = true; break;
                }
            }
            if (!found) new_unassigned.push_back(node);
        }
        unassigned_nodes = new_unassigned; 
    }

    return best_clusters;
}
