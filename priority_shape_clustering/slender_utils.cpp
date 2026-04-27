#include "slender_utils.h"
#include <cmath>
#include <algorithm>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double calculateAngle(double lat1, double lon1, double lat2, double lon2) {
    double dLon = (lon2 - lon1) * M_PI / 180.0;
    double r_lat1 = lat1 * M_PI / 180.0;
    double r_lat2 = lat2 * M_PI / 180.0;

    double y = std::sin(dLon) * std::cos(r_lat2);
    double x = std::cos(r_lat1) * std::sin(r_lat2) - std::sin(r_lat1) * std::cos(r_lat2) * std::cos(dLon);
    return std::atan2(y, x); // Returns Radians
}

void computeSlenderMatrix(int N, const std::vector<double>& theta, const std::vector<double>& rho,
                          std::vector<std::vector<double>>& delta, double max_rho) {
    const double alpha1 = 0.9; // theta 
    const double alpha2 = 0.1; // rho

    for (int i = 0; i < N; i++) {
        for (int j = 0; j < N; j++) {
            double theta_ij = (M_PI - std::abs(M_PI - std::abs(theta[i] - theta[j]))) / M_PI;
            double rho_ij = std::abs(rho[i] - rho[j]) / (max_rho > 0 ? max_rho : 1.0);
            delta[i][j] = (alpha1 * theta_ij) + (alpha2 * rho_ij);
        }
    }
}
