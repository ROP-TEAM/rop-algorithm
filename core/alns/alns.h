#pragma once

#include "alns/solution.h"
#include "alns/adaptive_penalty.h"
#include <chrono>
#include <cstdint>

namespace hfvrptwb {
namespace alns {

struct ALNSConfig {
    int    segment_size    = 100;
    double reaction_factor = 0.1;
    double initial_temp    = 100.0;
    double cooling_rate    = 0.9995;
    int    score_best      = 33;
    int    score_better    = 9;
    int    score_accepted  = 13;
};

class ALNSSolver {
public:
    explicit ALNSSolver(ALNSConfig cfg = {}) : cfg_(cfg) {}

    ConstructionResult solve(
        const solver::SolveRequest& req,
        const ConstructionResult& initial,
        double fixed, double km,
        std::chrono::milliseconds budget,
        int reload_min = 0,
        uint32_t seed = 42);

private:
    ALNSConfig cfg_;
};

} // namespace alns
} // namespace hfvrptwb
