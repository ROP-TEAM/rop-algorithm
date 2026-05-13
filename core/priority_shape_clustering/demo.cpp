#include <iostream>
#include <vector>
#include <cmath>
#include <algorithm>
#include <numeric>
#include <random>
#include <cstdio>
#include <string>
#include "types.h"
#include "slender_utils.h"
#include "slender_solver.h"

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

// Format minutes-from-midnight as "HH:MM".
static std::string toHHMM(int minutes) {
    int h = minutes / 60;
    int m = minutes % 60;
    char buf[8];
    std::snprintf(buf, sizeof(buf), "%02d:%02d", h, m);
    return buf;
}

// Print arrival/departure timeline for one trip.
// Starts from the depot (index 0) at depot_start_min.
static int printTripTimeline(
    const std::vector<int>&                  node_ids,
    const std::vector<Node>&                 nodes,
    const std::vector<std::vector<double>>&  dur,
    int                                      depot_start_min)
{
    int cur      = 0;                  // current position (depot = 0)
    int cur_time = depot_start_min;    // current clock time (minutes)

    for (int id : node_ids) {
        int travel  = static_cast<int>(dur[cur][id]);
        int arrival = cur_time + travel;
        int wait    = 0;
        if (arrival < nodes[id].tw_start) {
            wait    = nodes[id].tw_start - arrival;
            arrival = nodes[id].tw_start;
        }
        int depart = arrival + nodes[id].service_time;
        bool late  = arrival > nodes[id].tw_end;

        std::printf(
            "      %-12s  arr %s  dep %s  TW [%s-%s]%s%s\n",
            nodes[id].name.c_str(),
            toHHMM(arrival).c_str(),
            toHHMM(depart).c_str(),
            toHHMM(nodes[id].tw_start).c_str(),
            toHHMM(nodes[id].tw_end).c_str(),
            wait > 0 ? ("  wait " + std::to_string(wait) + "m").c_str() : "",
            late      ? "  *** LATE ***" : "");

        cur      = id;
        cur_time = depart;
    }

    // Return leg to depot
    int return_travel  = static_cast<int>(dur[cur][0]);
    int return_arrival = cur_time + return_travel;
    std::printf("      %-12s  arr %s\n", "→ Depot", toHHMM(return_arrival).c_str());
    return return_arrival;
}

// Nearest-neighbour TSP distance (depot → nodes → depot) — for display only.
static double nnTspDistance(const std::vector<int>& node_ids,
                             const std::vector<std::vector<double>>& dist) {
    if (node_ids.empty()) return 0.0;
    std::vector<bool> visited(dist.size(), false);
    double total   = 0.0;
    int    current = 0;
    for (size_t step = 0; step < node_ids.size(); ++step) {
        double min_d = 1e18;
        int    best  = -1;
        for (int id : node_ids) {
            if (!visited[id] && dist[current][id] < min_d) {
                min_d = dist[current][id];
                best  = id;
            }
        }
        if (best != -1) { total += min_d; current = best; visited[best] = true; }
    }
    total += dist[current][0];
    return total;
}

// ---------------------------------------------------------------------------
// Main
// ---------------------------------------------------------------------------

int main() {
    constexpr double FIXED_COST_PER_VEHICLE = 550.0;
    constexpr double COST_PER_KM           =  4.0003;

    // ---- Fleet (capacity, max_orders_per_day no longer enforced) ------------
    std::vector<Vehicle> fleet = {
        {"V-01",  50, 420,1020},
        {"V-02",  80, 360,1080},
        {"V-03",  10, 480,1260},
        {"V-04",  16, 420,1020},
        {"V-05",  12, 540, 900},
        {"V-06",  47, 480,1080},
        {"V-07",  40, 480,1020},
        {"V-08", 200, 300, 900},
        {"V-09",  60, 540,1140},
        {"V-10",  10, 540,1020},
        {"V-11", 100, 240, 840},
        {"V-12",  20, 420,1020},
    };

    // ---- Nodes (depot index 0; orders 1–30 with synthetic time windows) -----
    // tw_start/tw_end in minutes-from-midnight; service_time in minutes.
    // Orders are given a 3-hour window spread across a 7:00–17:00 workday.
    std::vector<Node> nodes = {
        {0,   "Depot",    16.4442,  102.8352,  0.0,  0,  0,   1440, 0, 0},
        {1, "ORD-A01",    16.43502, 102.836,   3,    10, 510, 900,  2, 0},
        {2, "ORD-A02",    16.47822, 102.823,   12,   25, 540, 1020, 1, 0},
        {3, "ORD-A03",    16.48042, 102.81194, 5,    15, 780, 960,  3, 0},
        {4, "ORD-A04",    16.4802,  102.81585, 1,    5,  480, 600,  4, 0},
        {5, "ORD-A05",    16.48683, 102.816,   8,    12, 600, 840,  2, 0},
        {6, "ORD-A06",    16.48951, 102.8189,  15,   30, 480, 960,  1, 0},
        {7, "ORD-A07",    16.48245, 102.8202,  4,    8,  660, 900,  2, 0},
        {8, "ORD-A08",    16.46055, 102.8261,  2,    10, 540, 660,  3, 0},
        {9, "ORD-A09",    16.48595, 102.8431,  10,   20, 480, 720,  2, 0},
        {10,"ORD-A10",   16.44659, 102.8239,  6,    15, 780, 1020, 1, 0},
        {11,"ORD-A11",   16.44827, 102.8347,  3,    12, 510, 690,  4, 0},
        {12,"ORD-A12",   16.4265,  102.8398,  7,    10, 600, 960,  2, 0},
        {13,"ORD-A13",   16.43688, 102.8385,  9,    20, 540, 900,  3, 0},
        {14,"ORD-A14",   16.47432, 102.8599,  2,    5,  840, 1020, 1, 0},
        {15,"ORD-A15",   16.44016, 102.8296,  11,   25, 480, 900,  2, 0},
        {16,"ORD-A16",   16.41872, 102.8324,  5,    15, 600, 720,  3, 0},
        {17,"ORD-A17",   16.46897, 102.8296,  4,    10, 780, 960,  1, 0},
        {18,"ORD-A18",   16.46596, 102.8255,  8,    18, 540, 840,  2, 0},
        {19,"ORD-A19",   16.428,   102.8339,  1,    5,  480, 600,  4, 0},
        {20,"ORD-A20",   16.47772, 102.8561,  14,   35, 660, 1020, 1, 0},
        {21,"ORD-A21",   16.44829, 102.8335,  6,    12, 570, 810,  2, 0},
        {22,"ORD-A22",   16.46358, 102.8277,  3,    10, 840, 960,  3, 0},
        {23,"ORD-A23",   16.4579,  102.8457,  12,   25, 480, 720,  2, 0},
        {24,"ORD-A24",   16.48063, 102.8685,  5,    15, 600, 900,  1, 0},
        {25,"ORD-A25",   16.45243, 102.7959,  2,    8,  480, 660,  3, 0},
        {26,"ORD-A26",   16.48062, 102.8186,  7,    12, 780, 1020, 2, 0},
        {27,"ORD-A27",   16.42653, 102.8285,  10,   20, 540, 960,  1, 0},
        {28,"ORD-A28",   16.43122, 102.8326,  4,    10, 660, 780,  4, 0},
        {29,"ORD-A29",   16.49159, 102.8328,  6,    15, 510, 870,  2, 0},
        {30,"ORD-A30",   16.45311, 102.8329,  1,    5,  900, 1020, 1, 0},
    };

    const int N     = static_cast<int>(nodes.size());
    const int K_max = static_cast<int>(fleet.size());

    // ---- Distance matrix (Euclidean km) -------------------------------------
    std::vector<std::vector<double>> distMatrix(N, std::vector<double>(N, 0.0));
    for (int i = 0; i < N; ++i) {
        for (int j = 0; j < N; ++j) {
            if (i == j) continue;
            double dy = (nodes[i].lat - nodes[j].lat) * 111.32;
            double dx = (nodes[i].lon - nodes[j].lon) * 111.32
                      * std::cos(nodes[i].lat * 0.01745329);
            distMatrix[i][j] = std::sqrt(dx * dx + dy * dy);
        }
    }

    // ---- Duration matrix (minutes) — treat km ≈ minutes at ~60 km/h --------
    // For a real deployment, replace with actual travel-time data.
    std::vector<std::vector<double>> dur = distMatrix;

    // ---- Slender matrix (spatial similarity) --------------------------------
    std::vector<double> theta(N, 0.0), rho(N, 0.0);
    double max_rho = 0.0;
    for (int i = 1; i < N; ++i) {
        theta[i] = calculateAngle(nodes[0].lat, nodes[0].lon,
                                  nodes[i].lat, nodes[i].lon);
        rho[i]   = distMatrix[0][i];
        if (rho[i] > max_rho) max_rho = rho[i];
    }
    std::vector<std::vector<double>> delta(N, std::vector<double>(N, 0.0));
    computeSlenderMatrix(N, theta, rho, delta, max_rho);

    // ---- Search loop: try different K and random vehicle subsets ------------
    std::mt19937 rng(std::random_device{}());

    double best_cost = 1e18;
    double best_dist = 0.0;
    int    best_K    = -1;
    std::vector<std::vector<Cluster>> best_plan;
    std::vector<int>                  best_subset;

    for (int attempt = 0; attempt < 10000; ++attempt) {
        // Random K in [1, K_max]
        int K = std::uniform_int_distribution<int>(1, K_max)(rng);

        std::vector<int> indices(K_max);
        std::iota(indices.begin(), indices.end(), 0);
        std::shuffle(indices.begin(), indices.end(), rng);
        std::vector<int> subset(indices.begin(), indices.begin() + K);

        // Capacities for this subset
        std::vector<double> caps(K);
        for (int v = 0; v < K; ++v)
            caps[v] = fleet[subset[v]].capacity;

        // Multi-trip assignment until all orders are served
        std::vector<int>                  unassigned(N - 1);
        std::iota(unassigned.begin(), unassigned.end(), 1);
        // Sort: deadline ASC (0 → last), then priority DESC — matches evaluateSubset
        std::stable_sort(unassigned.begin(), unassigned.end(), [&](int a, int b) {
            int da = nodes[a].deadline_min == 0 ? 999999 : nodes[a].deadline_min;
            int db = nodes[b].deadline_min == 0 ? 999999 : nodes[b].deadline_min;
            if (da != db) return da < db;
            return nodes[a].priority > nodes[b].priority;
        });

        std::vector<std::vector<Cluster>> vehicle_plan(K);
        std::vector<double>               ready_times(K);
        for (int k = 0; k < K; ++k)
            ready_times[k] = static_cast<double>(fleet[subset[k]].shift_start);
        bool feasible = true;

        while (!unassigned.empty()) {
            size_t before = unassigned.size();

            std::vector<Cluster> round_clusters =
                SlenderSolver::runOneRound(nodes, delta, dur, caps, unassigned, ready_times);

            if (unassigned.size() == before) { feasible = false; break; }

            for (int ci = 0; ci < (int)round_clusters.size() && ci < K; ++ci) {
                if (round_clusters[ci].node_ids.empty()) continue;
                vehicle_plan[ci].push_back(round_clusters[ci]);

                double t = ready_times[ci];
                int    cur = 0;
                for (int id : round_clusters[ci].node_ids) {
                    double arr = t + dur[cur][id];
                    if (arr < nodes[id].tw_start) arr = nodes[id].tw_start;
                    t   = arr + nodes[id].service_time;
                    cur = id;
                }
                ready_times[ci] = t + dur[cur][0];
                if (ready_times[ci] > fleet[subset[ci]].shift_end)
                    caps[ci] = 0.0;  // shift exhausted — block further assignment
            }
        }

        if (!feasible) continue;

        // Distance: sum NN-TSP over all trips (display only; insertion order
        // already time-feasible)
        double total_dist = 0.0;
        for (int v = 0; v < K; ++v)
            for (auto& trip : vehicle_plan[v])
                total_dist += nnTspDistance(trip.node_ids, distMatrix);

        double total_cost = K * FIXED_COST_PER_VEHICLE + total_dist * COST_PER_KM;

        if (total_cost < best_cost) {
            best_cost   = total_cost;
            best_dist   = total_dist;
            best_K      = K;
            best_plan   = vehicle_plan;
            best_subset = subset;

            std::cout << "\n=== NEW BEST | K=" << K
                      << " | dist=" << best_dist << " km"
                      << " | cost=" << best_cost << " THB ===\n";
            for (int v = 0; v < K; ++v) {
                if (vehicle_plan[v].empty()) continue;
                const Vehicle& veh = fleet[subset[v]];
                std::cout << veh.id << " (cap " << veh.capacity
                          << " kg, shift " << toHHMM(veh.shift_start)
                          << "-" << toHHMM(veh.shift_end) << "):\n";
                int t          = 1;
                int trip_start = veh.shift_start;
                for (const auto& trip : vehicle_plan[v]) {
                    std::printf("  Trip %d | weight=%.1f kg | stops=%zu\n",
                                t++, trip.total_weight, trip.node_ids.size());
                    trip_start = printTripTimeline(trip.node_ids, nodes, dur, trip_start);
                }
            }
        }
    }

    if (best_K == -1)
        std::cout << "No feasible solution found.\n";

    return 0;
}
