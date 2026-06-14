#pragma once
#include <string>
#include "solver_service.h"

class ConfigManager {
public:
static SolveConfig loadMode(float w_dist, float w_cost)
{
    SolveConfig cfg;

    cfg.fixedCostPerVehicle = 550.0;
    cfg.costPerKm           = 4.0003;
  
    cfg.weight_fixed_cost = 0.0;
    cfg.weight_per_km     = 100.0;
    cfg.alns_forbid_new_vehicle_prob = 0.0;

    cfg.multiStartCount = 6;
    return cfg;
}
};
