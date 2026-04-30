#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <string>

struct Node {
    int id;
    std::string name;
    double lat, lon;
    double weight;
    int priority = 0;      // 0=none 1=low 2=medium 3=high 4=critical
    int deadline_min = 0;  // minutes from midnight; 0 = no deadline
};

struct Vehicle {
    int id;
    std::string type;
    double capacity;
    int max_orders_per_day;
};

struct Cluster {
    int center_id;
    std::vector<int> node_ids;
    std::vector<int> route;      // node IDs in optimized visit order
    double total_weight = 0.0;
    int best_rank = 9999;
    double distance = 0.0;
};

#endif
