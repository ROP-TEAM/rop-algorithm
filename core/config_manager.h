#pragma once
#include <string>
#include "solver_service.h"

class ConfigManager {
public:
static SolveConfig loadMode(float w_dist, float w_cost) {
    SolveConfig cfg;
    cfg.fixedCostPerVehicle = 550.0;
    cfg.costPerKm           = 4.0003;

    cfg.weight_fixed_cost = 550.0 * w_cost;          
    cfg.weight_per_km     = 1.0   * w_dist
                          + 4.0003 * w_cost;        

    cfg.weight_wait_time  = 0.0;
    cfg.weight_under_fill = 0.0;
    cfg.unassigned_penalty = 5000;
    cfg.reloadMin = 0;                    

    cfg.alns_forbid_new_vehicle_prob =
        w_cost > 0.5f ? 0.5f : 0.1f;
    cfg.multiStartCount = 9;
    return cfg;
}
};
