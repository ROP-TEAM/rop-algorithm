#include "slender_solver.h"
#include "slender_utils.h"
#include <algorithm>
#include <random>
#include <unordered_set>

std::vector<Cluster> SlenderSolver::kMedoidsIterate(
    const std::vector<Node>& candidate_nodes,
    std::vector<int>& centers,
    const std::vector<std::vector<double>>& delta,
    const std::vector<double>& active_capacities,
    const std::vector<int>& active_remaining_orders)
{
    int K = centers.size();
    std::vector<Cluster> clusters(K);

    bool changed = true;
    int iter = 0;
    while (changed && iter < 20) {
        for (int k = 0; k < K; ++k) {
            clusters[k].node_ids.clear();
            clusters[k].total_weight = 0.0;
            clusters[k].center_id = centers[k];
        }

        for (const auto& node : candidate_nodes) {
            double min_score = 1e18;
            int best_k = -1;
            for (int k = 0; k < K; ++k) {
                if (clusters[k].total_weight + node.weight <= active_capacities[k] &&
                    (int)clusters[k].node_ids.size() < active_remaining_orders[k]) {
                    double score = delta[node.id][centers[k]];
                    if (score < min_score) { min_score = score; best_k = k; }
                }
            }
            if (best_k != -1) {
                clusters[best_k].node_ids.push_back(node.id);
                clusters[best_k].total_weight += node.weight;
            }
        }

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
    return clusters;
}

std::vector<Cluster> SlenderSolver::runOneRound(
    const std::vector<Node>& nodes,
    const std::vector<std::vector<double>>& delta,
    const std::vector<double>& active_capacities,
    const std::vector<int>& active_remaining_orders,
    std::vector<int>& unassigned_nodes)
{
    int K = active_capacities.size();
    if (K == 0 || unassigned_nodes.empty()) return {};

    std::vector<Node> candidates;
    candidates.reserve(unassigned_nodes.size());
    for (int idx : unassigned_nodes)
        candidates.push_back(nodes[idx]);

    std::vector<int> pool;
    pool.reserve(candidates.size());
    for (const auto& n : candidates) pool.push_back(n.id);

    std::vector<Cluster> best_clusters;
    double best_score = 1e18;
    std::mt19937 rng(42);

    for (int run = 0; run < 20; ++run) {
        std::vector<int> shuffled = pool;
        std::shuffle(shuffled.begin(), shuffled.end(), rng);
        std::vector<int> centers(K);
        for (int k = 0; k < K; ++k) centers[k] = shuffled[k % shuffled.size()];

        std::vector<Cluster> current = kMedoidsIterate(candidates, centers, delta, active_capacities, active_remaining_orders);

        double current_score = 0;
        int assigned_count = 0;
        for (const auto& c : current)
            for (int id : c.node_ids) { current_score += delta[id][c.center_id]; assigned_count++; }
        if (current_score < best_score && assigned_count > 0) {
            best_score = current_score;
            best_clusters = current;
        }
    }

    if (!best_clusters.empty()) {
        std::unordered_set<int> assigned;
        for (const auto& c : best_clusters)
            for (int id : c.node_ids) assigned.insert(id);
        std::vector<int> new_unassigned;
        for (int node : unassigned_nodes)
            if (!assigned.count(node)) new_unassigned.push_back(node);
        unassigned_nodes = std::move(new_unassigned);
    }

    return best_clusters;
}

std::vector<Cluster> SlenderSolver::clusterWithCenters(
    const std::vector<Node>& candidate_nodes,
    const std::vector<Node>& all_nodes,
    const std::vector<int>& centers,
    const std::vector<std::vector<double>>& distMatrix,
    const std::vector<double>& active_capacities,
    const std::vector<int>& active_remaining_orders)
{
    if (centers.empty() || candidate_nodes.empty()) return {};

    int N = all_nodes.size();
    std::vector<double> theta(N), rho(N);
    double max_rho = 0;
    for (int i = 1; i < N; ++i) {
        theta[i] = calculateAngle(all_nodes[0].lat, all_nodes[0].lon, all_nodes[i].lat, all_nodes[i].lon);
        rho[i] = distMatrix[0][i];
        if (rho[i] > max_rho) max_rho = rho[i];
    }
    std::vector<std::vector<double>> delta(N, std::vector<double>(N));
    computeSlenderMatrix(N, theta, rho, delta, max_rho);

    std::vector<int> mutable_centers = centers;
    return kMedoidsIterate(candidate_nodes, mutable_centers, delta, active_capacities, active_remaining_orders);
}
