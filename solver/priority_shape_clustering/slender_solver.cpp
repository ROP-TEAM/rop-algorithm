#include "slender_solver.h"
#include <algorithm>
#include <random>
#include <chrono>
// #include <iostream> // cout for debug

void SlenderSolver::assign(int N, int K, double capacity, const std::vector<Node>& nodes,
                           const std::vector<int>& centers, const std::vector<std::vector<double>>& delta,
                           std::vector<Cluster>& clusters) {
    for (int k = 0; k < K; ++k) {
        clusters[k].node_ids.clear();
        clusters[k].center_id = centers[k];
        clusters[k].node_ids.push_back(centers[k]);
        clusters[k].total_weight = nodes[centers[k]].weight;
    }

    std::vector<bool> assigned(N, false);
    assigned[0] = true; 
    for (int c : centers) assigned[c] = true;

    for (int i = 1; i < N; ++i) {
        if (assigned[i]) continue;
        double min_score = 1e18; 
        int best_k = -1;
        for (int k = 0; k < K; ++k) {
            if (clusters[k].total_weight + nodes[i].weight <= capacity) {
                if (delta[i][clusters[k].center_id] < min_score) {
                    min_score = delta[i][clusters[k].center_id];
                    best_k = k;
                }
            }
        }
        if (best_k != -1) {
            clusters[best_k].node_ids.push_back(i);
            clusters[best_k].total_weight += nodes[i].weight;
            assigned[i] = true;
        }
    }
}

bool SlenderSolver::update(int K, std::vector<int>& centers, const std::vector<Cluster>& clusters,
                           const std::vector<std::vector<double>>& delta) {
    bool changed = false;
    for (int k = 0; k < K; ++k) {
        if (clusters[k].node_ids.empty()) continue;
        int old_c = centers[k], best_c = old_c;
        double min_sum = 1e18;

        for (int candidate : clusters[k].node_ids) {
            double current_sum = 0;
            for (int member : clusters[k].node_ids) {
                current_sum += delta[candidate][member];
            }
            if (current_sum < min_sum) {
                min_sum = current_sum;
                best_c = candidate;
            }
        }
        if (best_c != old_c) {
            centers[k] = best_c;
            changed = true;
        }
    }
    return changed;
}

std::vector<Cluster> SlenderSolver::run(int N, int K, double capacity, 
                                        const std::vector<Node>& nodes, 
                                        const std::vector<std::vector<double>>& delta) {
    std::vector<Cluster> best_clusters;
    double best_obj = 1e18;
    std::vector<int> pool;
    for (int i = 1; i < N; ++i) pool.push_back(i);

    const int NUM_RESTARTS = 50; 

    for (int run = 0; run < NUM_RESTARTS; ++run) {
        // std::cout << "Restart " << (run + 1) << "/" << NUM_RESTARTS << "..." << std::endl;

        std::shuffle(pool.begin(), pool.end(), std::mt19937(std::chrono::system_clock::now().time_since_epoch().count() + run));
        std::vector<int> centers;
        for(int k=0; k<K; ++k) centers.push_back(pool[k]);

        std::vector<Cluster> clusters(K);

        bool changed = true;
        int iter_guard = 0; // prevent infinite loop
        while (changed && iter_guard < 50) { 
            assign(N, K, capacity, nodes, centers, delta, clusters);
            changed = update(K, centers, clusters, delta);
            iter_guard++;
        }

        double score = 0;
        int assigned_count = 0;
        for (auto& c : clusters) {
            for (int id : c.node_ids) {
                score += delta[id][c.center_id];
                assigned_count++;
            }
        }

        // if MT VRP not have assigned_count because we group it all.
        if (score < best_obj && assigned_count >= (N-1)) { 
            best_obj = score;
            best_clusters = clusters;
        }
    }

    // Priority Cluster by node that has most Priority
    for (auto& c : best_clusters) {
      c.best_rank = 9999;
      for (int id : c.node_ids) {
        if (id < c.best_rank) {
          c.best_rank = id;
        }
      }
    }

    // Sort It!
    std::sort(best_clusters.begin(), best_clusters.end(), [](const Cluster& a, const Cluster& b) {
        return a.best_rank < b.best_rank;
        });

    return best_clusters;
}
