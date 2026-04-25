#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <string>

struct Node {
    int id;
    std::string name;
    double lat, lon;
    double weight;
    // int priority; 
};

struct Vehicle {
    int id;
    std::string type;
    double capacity; // Each vehicle can have different capacity later
};

struct Cluster {
    int center_id;             // Current Medoid ID
    std::vector<int> node_ids; // Assigned customers
    double total_weight = 0.0;
    // int vehicle_id;            // Linked vehicle
    int best_rank = 9999;      // store the most priority index in this cluster
};

#endif
