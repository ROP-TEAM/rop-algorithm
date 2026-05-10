#pragma once

#include "alns/solution.h"

namespace hfvrptwb {
namespace alns {

void tripMerge(ALNSSolution&, const solver::SolveRequest&,
               double fixed, double km);

void customerMoveAcrossTrips(ALNSSolution&, const solver::SolveRequest&,
                              double fixed, double km);

void twoOptStar(ALNSSolution&, const solver::SolveRequest&,
                double fixed, double km);

void relocateAcrossVehicles(ALNSSolution&, const solver::SolveRequest&,
                             double fixed, double km);

void consolidateVehicles(ALNSSolution&, const solver::SolveRequest&,
                          double fixed, double km);

void swapStar(ALNSSolution&, const solver::SolveRequest&,
              double fixed, double km);

void applyLocalSearch(ALNSSolution&, const solver::SolveRequest&,
                      double fixed, double km);

} // namespace alns
} // namespace hfvrptwb
