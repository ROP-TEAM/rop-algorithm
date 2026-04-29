#ifndef TYPES_H
#define TYPES_H

#include <vector>
#include <string>

struct Node {
    int id;
    std::string name;
    double lat, lon;
    double weight;
};

struct Vehicle {
    int id;
    std::string type;
    double capacity; 
    int max_orders_per_day; // จำกัดจำนวนโหนดสูงสุดที่รับได้ต่อวัน
};

struct Cluster {
    int center_id;             
    std::vector<int> node_ids; 
    double total_weight = 0.0;
    int best_rank = 9999;      
    double distance = 0.0;     // ระยะทางที่ได้จาก TSP
};

#endif
