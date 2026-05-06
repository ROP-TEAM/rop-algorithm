#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <string>

struct Node {
    int id;
    std::string name;
    double lat, lon;
    double weight;
    int service_time = 0;     // service duration at this node (minutes)
    int tw_start     = 0;     // hard TW open  (minutes from midnight)
    int tw_end       = 1440;  // hard TW close (minutes from midnight); default = end of day
    int priority     = 0;     // 0=none 1=low 2=medium 3=high 4=critical
    int deadline_min = 0;     // minutes from midnight; 0 = no deadline
};

struct Vehicle {
    std::string id;
    double capacity;
    int shift_start = 0;     // earliest depot departure (minutes from midnight)
    int shift_end   = 1440;  // must return to depot by this time
};

struct Cluster {
    int              center_id    = -1;
    std::vector<int> node_ids;          // visit order: medoid first, then greedy-appended nodes
    std::vector<int> route;             // post-optimised visit order (if TSP applied)
    double           total_weight = 0.0;
    int              best_rank    = 9999;
    double           distance     = 0.0;

    // Macro Node time state (updated incrementally as nodes are appended)
    double E            = 0.0;    // earliest feasible departure-start of this sequence
    double L            = 1440.0; // latest  feasible departure-start of this sequence
    double S            = 0.0;    // accumulated service + travel time within sequence
    int    last_node_id = -1;     // id of the last appended node (travel-time lookup)
};

#endif
