#include "solver_service.h"
#include "solver.pb.h"
#include <iostream>
#include <iomanip>
#include <cmath>

// Simple Euclidean distance in meters between two lat/lon points (flat-earth approx)
static double metersBetween(double lat1, double lon1, double lat2, double lon2) {
    double dx = (lon2 - lon1) * 111320.0 * std::cos(lat1 * M_PI / 180.0);
    double dy = (lat2 - lat1) * 110540.0;
    return std::sqrt(dx * dx + dy * dy);
}

// Travel time in minutes at 40 km/h
static double minutesBetween(double lat1, double lon1, double lat2, double lon2) {
    return metersBetween(lat1, lon1, lat2, lon2) / 1000.0 / 40.0 * 60.0;
}

int main() {
    // Nodes: depot at center, 4 delivery nodes spread around Bangkok area
    struct Loc { const char* id; double lat, lon; int demand, priority, deadline_min, service_time; };
    Loc depot = { "depot", 13.756, 100.502, 0, 0, 0, 0 };
    Loc nodes[] = {
        { "A", 13.774, 100.502,  10, 4, 600,  5 },  // critical, deadline 10:00
        { "B", 13.756, 100.520,  15, 1,   0, 10 },  // low priority, no deadline
        { "C", 13.738, 100.502,  20, 2,   0,  5 },  // medium priority
        { "D", 13.756, 100.484,   5, 3, 720,  5 },  // high, deadline 12:00
    };
    int N = 5; // depot + 4 nodes

    // All locations in order: [depot, A, B, C, D]
    double lats[] = { depot.lat, nodes[0].lat, nodes[1].lat, nodes[2].lat, nodes[3].lat };
    double lons[] = { depot.lon, nodes[0].lon, nodes[1].lon, nodes[2].lon, nodes[3].lon };

    solver::SolveRequest req;

    // Depot
    auto* d = req.mutable_depot();
    d->set_id(depot.id);
    d->set_lat(depot.lat);
    d->set_lng(depot.lon);

    // Delivery nodes
    for (const auto& n : nodes) {
        auto* pn = req.add_nodes();
        pn->set_id(n.id);
        pn->set_lat(n.lat);
        pn->set_lng(n.lon);
        pn->set_demand(n.demand);
        pn->set_priority(n.priority);
        pn->set_deadline_min(n.deadline_min);
        pn->set_service_time(n.service_time);
    }

    // Vehicle
    auto* v = req.add_vehicles();
    v->set_id("truck-1");
    v->set_capacity(100);
    v->set_max_tasks(0);   // unlimited
    v->set_shift_start(480); // 08:00

    // Flatten N×N distance + duration matrices (row-major)
    req.set_matrix_size(N);
    for (int i = 0; i < N; ++i)
        for (int j = 0; j < N; ++j) {
            req.add_distances(metersBetween(lats[i], lons[i], lats[j], lons[j]));
            req.add_durations(minutesBetween(lats[i], lons[i], lats[j], lons[j]));
        }

    // Solve
    SolverServiceImpl service;
    solver::SolveResponse resp;
    service.Solve(nullptr, &req, &resp);

    // Print results
    std::cout << "Status: " << resp.status() << "\n";
    std::cout << "Objective (total distance m): " << std::fixed << std::setprecision(1)
              << resp.objective() << "\n\n";

    for (const auto& route : resp.routes()) {
        std::cout << "Vehicle: " << route.vehicle_id() << "\n";
        std::cout << "  " << std::left << std::setw(8) << "Node"
                  << std::setw(12) << "Arrive" << "Depart\n";
        for (const auto& stop : route.stops()) {
            auto toHHMM = [](int m) -> std::string {
                return std::to_string(m / 60) + ":" + (m % 60 < 10 ? "0" : "") + std::to_string(m % 60);
            };
            std::cout << "  " << std::setw(8) << stop.node_id()
                      << std::setw(12) << toHHMM(stop.arrival_min())
                      << toHHMM(stop.depart_min()) << "\n";
        }
        std::cout << "  Total dist: " << std::setprecision(1) << route.total_distance() << " m"
                  << "  |  Duration: " << route.total_duration() << " min\n";
    }

    if (resp.unassigned_size() > 0) {
        std::cout << "\nUnassigned:";
        for (const auto& u : resp.unassigned()) std::cout << " " << u;
        std::cout << "\n";
    }

    return 0;
}
