#pragma once

#include "alns/solution.h"
#include "alns/adaptive_penalty.h"
#include <chrono>
#include <cstdint>
#include <vector>

namespace hfvrptwb {
namespace alns {

struct ALNSConfig {
    int    segment_size          = 40;
    double reaction_factor       = 0.1;
    double initial_temp          = 100.0;
    double cooling_rate          = 0.9995;
    double min_temp              = 1.0;
    double reheat_temp           = 50.0;
    int    score_best            = 33;
    int    score_better          = 9;
    int    score_accepted        = 13;
    bool   enable_sector_removal = true;
    double forbid_new_vehicle_prob = 0.5;
};

struct OperatorStats {
    std::vector<double> final_weights;
    std::vector<int>    selection_count;
    std::vector<int>    improve_count;   // accepted moves where obj improved (delta < 0)
    std::vector<int>    best_count;      // accepted moves that set new global best
};

// Operator names for reporting; index matches destroyers[] order.
const char* destroyOperatorName(int index);

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

    const OperatorStats& stats() const { return stats_; }

private:
    ALNSConfig    cfg_;
    OperatorStats stats_;
};

} // namespace alns
} // namespace hfvrptwb
