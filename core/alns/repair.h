#pragma once

#include "alns/solution.h"

namespace hfvrptwb {
namespace alns {

void greedyRepair(ALNSSolution&, const solver::SolveRequest&,
                  double fixed, double km, int reload_min = 0);

void priorityFirstRepair(ALNSSolution&, const solver::SolveRequest&,
                          double fixed, double km, int reload_min = 0);

void regret2Repair(ALNSSolution&, const solver::SolveRequest&,
                    double fixed, double km, int reload_min = 0);

void regret3Repair(ALNSSolution&, const solver::SolveRequest&,
                    double fixed, double km, int reload_min = 0);

void proactiveBreakInsertion(ALNSSolution&, const solver::SolveRequest&,
                              double fixed, double km, int reload_min = 0);

} // namespace alns
} // namespace hfvrptwb
