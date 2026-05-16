#pragma once
#include <string>
#include "solver_service.h"

class ConfigManager {
public:
    static SolveConfig loadMode(const std::string& mode_name) {
        SolveConfig cfg;

        if (mode_name == "MIN_DISTANCE") {
          cfg.weight_fixed_cost  = 0.0;    // don't care how many vehicles
          cfg.weight_per_km      = 1.0;    // only thing that matters
          cfg.weight_wait_time   = 0.0;
          cfg.weight_under_fill  = 0.0;
          cfg.weight_unbalance   = 0.0;

        } else if (mode_name == "MIN_WAIT_TIME") {
          cfg.weight_fixed_cost = 100.0;
          cfg.weight_per_km = 4.0003;
          cfg.weight_wait_time = 100.0;
          cfg.weight_under_fill = 0.0;

        } else if (mode_name == "BALANCE_ALL_CARS") {
          cfg.weight_fixed_cost = -1000.0;
          cfg.weight_per_km = 2.0;
          cfg.weight_unbalance = 500.0;
        }
        else {
          // "MIN_COST"
          cfg.weight_fixed_cost = 550.0;
          cfg.weight_per_km = 4.0003;
          cfg.weight_wait_time = 2.0;
          cfg.weight_under_fill = 500.0;
        }

        return cfg;
    }
};
