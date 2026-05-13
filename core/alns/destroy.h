#pragma once

#include "alns/solution.h"
#include <random>

namespace hfvrptwb {
namespace alns {

void removeNode(ALNSSolution& sol, int vi, int ti, int node_index,
                const solver::SolveRequest& req, double fixed, double km);

void randomRemoval(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                   double fixed, double km, int q = 4);

void worstRemoval(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                  double fixed, double km, int q = 4);

void shawRemoval(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                 double fixed, double km, int q = 4);

void priorityAwareRemoval(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                           double fixed, double km, int q = 4);

void routeConsolidationDestroy(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                               double fixed, double km, int q = 4);

void tripRemoval(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                 double fixed, double km, int q = 4);

void tagViolationRemoval(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                          double fixed, double km, int q = 4);

void lateCustomerRemoval(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                          double fixed, double km, int q = 4);

void sectorRemoval(ALNSSolution&, std::mt19937&, const solver::SolveRequest&,
                   double fixed, double km, int q = 4);

} // namespace alns
} // namespace hfvrptwb
