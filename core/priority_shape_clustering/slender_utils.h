#ifndef SLENDER_UTILS_H
#define SLENDER_UTILS_H

#include <vector>
#include <cmath>
#include "types.h"

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

double calculateAngle(double lat1, double lon1, double lat2, double lon2);

void computeSlenderMatrix(int N, 
                          const std::vector<double>& theta, 
                          const std::vector<double>& rho,
                          std::vector<std::vector<double>>& delta, 
                          double max_rho);

#endif
