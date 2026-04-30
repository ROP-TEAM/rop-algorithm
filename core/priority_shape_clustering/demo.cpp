#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include "types.h"
#include "slender_utils.h"
#include "slender_solver.h"

// dummy TSP (Nearest Neighbour) for comparison
double calculateTSPRoute(const std::vector<int>& node_ids,
                         const std::vector<Node>& nodes,
                         const std::vector<std::vector<double>>& distMatrix) {
    if (node_ids.empty()) return 0.0;
    std::vector<int> route = node_ids;
    double total = 0.0;
    int current = 0;
    std::vector<bool> visited(nodes.size(), false);
    for (size_t i = 0; i < route.size(); ++i) {
        double min_dist = 1e18;
        int best = -1;
        for (int id : route) {
            if (!visited[id] && distMatrix[current][id] < min_dist) {
                min_dist = distMatrix[current][id];
                best = id;
            }
        }
        if (best != -1) {
            total += min_dist;
            current = best;
            visited[best] = true;
        }
    }
    total += distMatrix[current][0];
    return total;
}

int main() {
    const double FIXED_COST_PER_VEHICLE = 550.0;
    const double COST_PER_KM = 40.0;

    std::vector<Vehicle> fleet = {
        {1,  "Truck_1",  45.0, 14}, {2,  "Truck_2",  38.0, 12}, {3,  "Truck_3",  50.0, 15},
        {4,  "Truck_4",  30.0, 10}, {5,  "Truck_5",  60.0, 16}, {6,  "Truck_6",  42.0, 13},
        {7,  "Truck_7",  35.0, 11}, {8,  "Truck_8",  55.0, 17}, {9,  "Truck_9",  28.0,  9},
        {10, "Truck_10", 48.0, 14}, {11, "Truck_11", 33.0, 10}, {12, "Truck_12", 52.0, 16}
    };

    std::vector<Node> nodes = {
        {0,  "Depot",     16.4442, 102.8352, 0.0}, {1,  "BKK-K01",   16.435022, 102.836030, 15.0},
        {2,  "BKK-K02",   16.478216, 102.819988, 2.0}, {3,  "BKK-K03",   16.480415, 102.811911, 8.0},
        {4,  "BKK-K04",   16.480198, 102.815845, 5.0}, {5,  "BKK-K05",   16.486826, 102.816038, 10.0},
        {6,  "BKK-K06",   16.489513, 102.818906, 1.0}, {7,  "BKK-K07",   16.482446, 102.820245, 12.0},
        {8,  "BKK-K08",   16.460546, 102.826129, 4.0}, {9,  "BKK-K09",   16.485946, 102.843109, 7.0},
        {10, "BKK-K10",   16.446586, 102.823942, 3.0}, {11, "BKK-K11",   16.448274, 102.834671, 20.0},
        {12, "BKK-K12",   16.426499, 102.839821, 6.0}, {13, "BKK-K13",   16.436883, 102.838487, 9.0},
        {14, "BKK-K14",   16.474315, 102.859931, 5.0}, {15, "BKK-K15",   16.440155, 102.829593, 11.0},
        {16, "BKK-K16",   16.418724, 102.832380, 2.0}, {17, "BKK-K17",   16.468967, 102.829573, 8.0},
        {18, "BKK-K18",   16.465963, 102.825539, 14.0}, {19, "BKK-K19",   16.428000, 102.833915, 1.0},
        {20, "BKK-K20",   16.477720, 102.856120, 6.0}, {21, "BKK-K21",   16.448285, 102.833536, 10.0},
        {22, "BKK-K22",   16.463575, 102.827698, 4.0}, {23, "BKK-K23",   16.457895, 102.845722, 3.0},
        {24, "BKK-K24",   16.480634, 102.868522, 18.0}, {25, "BKK-K25",   16.452426, 102.795862, 7.0},
        {26, "BKK-K26",   16.480622, 102.818632, 5.0}, {27, "BKK-K27",   16.426529, 102.828486, 12.0},
        {28, "BKK-K28",   16.431222, 102.832606, 2.0}, {29, "BKK-K29",   16.491586, 102.832824, 9.0},
        {30, "BKK-K30",   16.453110, 102.832916, 6.0}
    };

    int N = nodes.size();
    int K_max = fleet.size();

    // Matrix
    std::vector<std::vector<double>> distMatrix(N, std::vector<double>(N));
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (i == j) distMatrix[i][j] = 0.0;
            else {
                double dy = (nodes[i].lat - nodes[j].lat) * 111.32;
                double dx = (nodes[i].lon - nodes[j].lon) * 111.32 * std::cos(nodes[i].lat * 0.01745);
                distMatrix[i][j] = std::sqrt(dx*dx + dy*dy);
            }
        }
    }

    std::vector<double> theta(N), rho(N);
    double max_rho = 0;
    for (int i = 1; i < N; ++i) {
        theta[i] = calculateAngle(nodes[0].lat, nodes[0].lon, nodes[i].lat, nodes[i].lon);
        rho[i] = distMatrix[0][i];
        if (rho[i] > max_rho) max_rho = rho[i];
    }
    std::vector<std::vector<double>> delta(N, std::vector<double>(N));
    computeSlenderMatrix(N, theta, rho, delta, max_rho);

    std::mt19937 rng(std::random_device{}());
    std::uniform_int_distribution<int> K_dist(1, K_max);

    double best_cost = 1e18;
    double best_dist = 0;
    int best_K = -1;
    std::vector<std::vector<Cluster>> best_plan;
    std::vector<int> best_subset;

    while (true) {
        int K = K_dist(rng);

        std::vector<int> indices(K_max);
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), rng);
        std::vector<int> subset(indices.begin(), indices.begin() + K);

        std::vector<int> unassigned(N - 1);
        std::iota(unassigned.begin(), unassigned.end(), 1);

        std::vector<int> orders_served(K, 0);
        std::vector<std::vector<Cluster>> vehicle_plan(K);
        bool feasible = true;

        while (!unassigned.empty()) {
            std::vector<int> active_indices;
            std::vector<double> active_caps;
            std::vector<int> active_rem_orders;

            for (int v = 0; v < K; ++v) {
                int left = fleet[subset[v]].max_orders_per_day - orders_served[v];
                if (left > 0) {
                    active_indices.push_back(v);
                    active_caps.push_back(fleet[subset[v]].capacity);
                    active_rem_orders.push_back(left);
                }
            }
            if (active_indices.empty()) { feasible = false; break; }

            int nodes_left_before = unassigned.size();
            std::vector<Cluster> new_clusters = SlenderSolver::runOneRound(
                nodes, delta, active_caps, active_rem_orders, unassigned
            );
            if (unassigned.size() == nodes_left_before) { feasible = false; break; }

            for (size_t i = 0; i < active_indices.size(); ++i) {
                int v = active_indices[i];
                if (new_clusters[i].node_ids.empty()) continue;
                vehicle_plan[v].push_back(new_clusters[i]);
                orders_served[v] += new_clusters[i].node_ids.size();
            }
        }

        if (!feasible) continue;

        double total_dist = 0.0;
        for (int v = 0; v < K; ++v) {
            for (auto& trip : vehicle_plan[v]) {
                trip.distance = calculateTSPRoute(trip.node_ids, nodes, distMatrix);
                total_dist += trip.distance;
            }
        }
        double total_cost = K * FIXED_COST_PER_VEHICLE + total_dist * COST_PER_KM;

        if (total_cost < best_cost) {
            best_cost = total_cost;
            best_dist = total_dist;
            best_K = K;
            best_plan = vehicle_plan;
            best_subset = subset;

            std::cout << "\n=======================================\n";
            std::cout << "NEW BEST | K=" << K << " | dist=" << best_dist << " km | cost=" << best_cost << " THB\n";
            for (int i = 0; i < K; ++i) {
                if (vehicle_plan[i].empty()) continue;
                const Vehicle& veh = fleet[subset[i]];
                std::cout << veh.type << " (Cap: " << veh.capacity << "kg):\n";
                int t = 1;
                for (const auto& trip : vehicle_plan[i]) {
                    std::cout << "  Trip " << t++ << " | Nodes: ";
                    for (int id : trip.node_ids) std::cout << nodes[id].name << " ";
                    std::cout << "| Weight: " << trip.total_weight << "kg | Dist: " << trip.distance << " km\n";
                }
            }
        }
    }

    return 0;
}
