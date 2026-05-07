#pragma once

#include "solver.pb.h"
#include <string>
#include <vector>

namespace hfvrptwb {

struct ConstructedRoute {
    int vehicle_index = -1;
    std::vector<int> nodes;
    double total_cost = 0.0;
};

struct ConstructionDrop {
    std::string node_id;
    std::string code;
    std::string detail;
};

struct ConstructionResult {
    std::vector<ConstructedRoute> routes;
    std::vector<ConstructionDrop> drops;
    bool timed_out = false;
};

ConstructionResult adaptiveConstruct(
    const solver::SolveRequest& req,
    double default_fixed_cost,
    double default_cost_per_km);

} // namespace hfvrptwb
