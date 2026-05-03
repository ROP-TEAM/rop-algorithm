#pragma once
#include "solver.pb.h"

struct BenchConfig {
    int    seed;
    int    num_orders;
    int    num_vehicles;
    double depot_lat;
    double depot_lon;
    double radius_deg;
    int    cap_min;
    int    cap_max;
    int    demand_min;
    int    demand_max;
    double max_dist_frac;    // fraction of vehicles that have max_distance
    double max_dist_m;       // max_distance value in metres
    double tag_coverage;     // probability each vehicle accepts each tag
    bool   enable_tw;
    int    tw_width_min;     // minutes
    int    tw_width_max;
    int    num_clusters;     // 0 or 1 = uniform; >1 = clustered geography
    double cluster_spread_deg;
};

solver::SolveRequest buildRequest(const BenchConfig& cfg);
