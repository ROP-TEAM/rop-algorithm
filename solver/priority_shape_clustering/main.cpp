#include <iostream>
#include <vector>
#include <format>
#include <cmath>
#include "types.h"
#include "slender_utils.h"
#include "slender_solver.h"

int main() {
    std::vector<Vehicle> fleet = {
        {1, "Truck_1", 50.0}, {2, "Truck_2", 50.0},
        {3, "Truck_3", 50.0}, {4, "Truck_4", 50.0}, {5, "Truck_5", 50.0}
    };

    std::vector<Node> nodes = {
        {0,  "Depot",     16.4442, 102.8352, 0.0}, 

        {1,  "BKK-K01",   16.435022, 102.836030, 15.0},
        {2,  "BKK-K02",   16.478216, 102.819988, 2.0},
        {3,  "BKK-K03",   16.480415, 102.811911, 8.0},
        {4,  "BKK-K04",   16.480198, 102.815845, 5.0},
        {5,  "BKK-K05",   16.486826, 102.816038, 10.0},
        {6,  "BKK-K06",   16.489513, 102.818906, 1.0},
        {7,  "BKK-K07",   16.482446, 102.820245, 12.0},
        {8,  "BKK-K08",   16.460546, 102.826129, 4.0},
        {9,  "BKK-K09",   16.485946, 102.843109, 7.0},
        {10, "BKK-K10",   16.446586, 102.823942, 3.0},
        {11, "BKK-K11",   16.448274, 102.834671, 20.0},
        {12, "BKK-K12",   16.426499, 102.839821, 6.0},
        {13, "BKK-K13",   16.436883, 102.838487, 9.0},
        {14, "BKK-K14",   16.474315, 102.859931, 5.0},
        {15, "BKK-K15",   16.440155, 102.829593, 11.0},
        {16, "BKK-K16",   16.418724, 102.832380, 2.0},
        {17, "BKK-K17",   16.468967, 102.829573, 8.0},
        {18, "BKK-K18",   16.465963, 102.825539, 14.0},
        {19, "BKK-K19",   16.428000, 102.833915, 1.0},
        {20, "BKK-K20",   16.477720, 102.856120, 6.0},
        {21, "BKK-K21",   16.448285, 102.833536, 10.0},
        {22, "BKK-K22",   16.463575, 102.827698, 4.0},
        {23, "BKK-K23",   16.457895, 102.845722, 3.0},
        {24, "BKK-K24",   16.480634, 102.868522, 18.0},
        {25, "BKK-K25",   16.452426, 102.795862, 7.0},
        {26, "BKK-K26",   16.480622, 102.818632, 5.0},
        {27, "BKK-K27",   16.426529, 102.828486, 12.0},
        {28, "BKK-K28",   16.431222, 102.832606, 2.0},
        {29, "BKK-K29",   16.491586, 102.832824, 9.0},
        {30, "BKK-K30",   16.453110, 102.832916, 6.0}
    };

    int N = nodes.size();
    int K = fleet.size();

    // Prep dis Matrix
    std::vector<std::vector<double>> distMatrix(N, std::vector<double>(N));
    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            if (i == j) distMatrix[i][j] = 0.0;
            else {
                double dy = (nodes[i].lat - nodes[j].lat) * 111.32;
                double dx = (nodes[i].lon - nodes[j].lon) * 111.32 * std::cos(nodes[i].lat * 0.01745);
                distMatrix[i][j] = std::sqrt(dx*dx + dy*dy);
            }
        }
    }

    // 4. Pre-calculate Slender Matrix
    std::vector<double> theta(N), rho(N);
    double max_rho = 0;
    for (int i = 1; i < N; ++i) {
        theta[i] = calculateAngle(nodes[0].lat, nodes[0].lon, nodes[i].lat, nodes[i].lon);
        rho[i] = distMatrix[0][i];
        if (rho[i] > max_rho) max_rho = rho[i];
    }
    std::vector<std::vector<double>> delta(N, std::vector<double>(N));
    computeSlenderMatrix(N, theta, rho, delta, max_rho);

    auto results = SlenderSolver::run(N, K, fleet[0].capacity, nodes, delta);

    std::cout << "--- SIMPLE RESULTS ---\n";
    for (int k = 0; k < K; ++k) {
      if (results[k].node_ids.empty()) continue;

      std::cout << "Vehicle " << k + 1 << " Nodes: [ ";

      for (int id : results[k].node_ids) {
        std::cout << id << " ";
      }

      std::cout << "]\n";
    }

    return 0;
}
