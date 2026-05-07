// bench_solve.cpp — benchmark harness for solver research
// How to extend: create a new *_bench.cpp that includes bench_gen.h and
// calls buildRequest() with your own BenchConfig scenarios.
#include "bench_gen.h"
#include "solver_service.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <string>
#include <vector>

struct RouteLog {
    std::string vehicle_id;
    int         num_stops;
    double      total_distance;
    int         total_duration;  // minutes
};

struct BenchResult {
    std::string              name;
    int                      num_orders;
    int                      num_vehicles;
    std::string              status;
    int                      assigned;
    double                   objective;
    double                   elapsed_ms;
    std::vector<RouteLog>    routes;
    std::vector<std::string> unassigned;
};

static BenchResult runScenario(const char* name, const BenchConfig& cfg) {
    auto req = buildRequest(cfg);
    SolverServiceImpl service;
    solver::SolveResponse resp;

    auto t0 = std::chrono::steady_clock::now();
    service.Solve(nullptr, &req, &resp);
    double ms = std::chrono::duration<double, std::milli>(
        std::chrono::steady_clock::now() - t0).count();

    BenchResult r;
    r.name         = name;
    r.num_orders   = cfg.num_orders;
    r.num_vehicles = cfg.num_vehicles;
    r.status       = resp.status();
    r.assigned     = cfg.num_orders - resp.unassigned_size();
    r.objective    = resp.objective();
    r.elapsed_ms   = ms;

    for (const auto& route : resp.routes()) {
        RouteLog rl;
        rl.vehicle_id     = route.vehicle_id();
        rl.num_stops      = route.stops_size();
        rl.total_distance = route.total_distance();
        rl.total_duration = route.total_duration();
        r.routes.push_back(rl);
    }

    for (const auto& id : resp.unassigned())
        r.unassigned.push_back(id);

    return r;
}

static void printTable(const std::vector<BenchResult>& results) {
    std::cout << std::left
              << std::setw(18) << "Scenario"
              << std::setw(6)  << "N"
              << std::setw(5)  << "V"
              << std::setw(12) << "Status"
              << std::setw(10) << "Assigned"
              << std::setw(12) << "Objective"
              << std::setw(10) << "ms"
              << "\n" << std::string(73, '-') << "\n";
    for (const auto& r : results) {
        std::cout << std::left
                  << std::setw(18) << r.name
                  << std::setw(6)  << r.num_orders
                  << std::setw(5)  << r.num_vehicles
                  << std::setw(12) << r.status
                  << std::setw(10) << (std::to_string(r.assigned) + "/" +
                                       std::to_string(r.num_orders))
                  << std::fixed << std::setprecision(0)
                  << std::setw(12) << r.objective
                  << std::setprecision(2)
                  << std::setw(10) << r.elapsed_ms
                  << "\n";
    }
}

static void writeJSON(const std::vector<BenchResult>& results,
                      const std::string& path) {
    std::ofstream f(path);
    f << "[\n";
    for (int i = 0; i < (int)results.size(); ++i) {
        const auto& r = results[i];
        f << "  {\n"
          << "    \"name\":\""       << r.name         << "\",\n"
          << "    \"n\":"            << r.num_orders    << ",\n"
          << "    \"v\":"            << r.num_vehicles  << ",\n"
          << "    \"status\":\""     << r.status        << "\",\n"
          << "    \"assigned\":"     << r.assigned      << ",\n"
          << "    \"total\":"        << r.num_orders    << ",\n"
          << "    \"objective\":"    << std::fixed << std::setprecision(2)
                                     << r.objective     << ",\n"
          << "    \"elapsed_ms\":"   << std::setprecision(3)
                                     << r.elapsed_ms    << ",\n"
          << "    \"routes\":[\n";
        for (int j = 0; j < (int)r.routes.size(); ++j) {
            const auto& rt = r.routes[j];
            f << "      {"
              << "\"vehicle_id\":\"" << rt.vehicle_id    << "\","
              << "\"stops\":"        << rt.num_stops     << ","
              << "\"distance\":"     << std::setprecision(2) << rt.total_distance << ","
              << "\"duration\":"     << rt.total_duration
              << "}";
            if (j + 1 < (int)r.routes.size()) f << ",";
            f << "\n";
        }
        f << "    ],\n"
          << "    \"unassigned\":[";
        for (int j = 0; j < (int)r.unassigned.size(); ++j) {
            f << "\"" << r.unassigned[j] << "\"";
            if (j + 1 < (int)r.unassigned.size()) f << ",";
        }
        f << "]\n"
          << "  }";
        if (i + 1 < (int)results.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

int main() {
    const double LAT = 16.4442, LON = 102.8352, R = 0.09;
    std::filesystem::create_directories("test");

    std::vector<BenchResult> results;

    // ── Baseline ────────────────────────────────────────────────────────────
    results.push_back(runScenario("baseline",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 1.0, false, 0, 0}));

    // ── Capacity stress ──────────────────────────────────────────────────────
    results.push_back(runScenario("cap_loose",
        {42, 100, 10, LAT, LON, R, 40, 60, 5, 10, 0.0, 0, 1.0, false, 0, 0}));
    results.push_back(runScenario("cap_medium",
        {42, 100, 10, LAT, LON, R, 20, 30, 5, 10, 0.0, 0, 1.0, false, 0, 0}));
    results.push_back(runScenario("cap_tight",
        {42, 100, 10, LAT, LON, R, 10, 15, 5, 10, 0.0, 0, 1.0, false, 0, 0}));
    results.push_back(runScenario("cap_stress",
        {42, 100, 10, LAT, LON, R, 10, 14, 8, 18, 0.0, 0, 1.0, false, 0, 0}));

    // ── Tag coverage ────────────────────────────────────────────────────────
    results.push_back(runScenario("tag_tc05",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.05, false, 0, 0}));
    results.push_back(runScenario("tag_tc10",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.10, false, 0, 0}));
    results.push_back(runScenario("tag_tc20",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.20, false, 0, 0}));
    results.push_back(runScenario("tag_tc50",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.50, false, 0, 0}));
    results.push_back(runScenario("tag_stress",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.10, false, 0, 0}));

    // ── Distance threshold ───────────────────────────────────────────────────
    results.push_back(runScenario("dist_sw_02",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.2, 3000, 1.0, false, 0, 0}));
    results.push_back(runScenario("dist_sw_05",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.5, 6000, 1.0, false, 0, 0}));
    results.push_back(runScenario("dist_sw_10",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 1.0, 5000, 1.0, false, 0, 0}));
    results.push_back(runScenario("dist_stress",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 1.0, 5000, 1.0, false, 0, 0}));

    // ── Time windows ─────────────────────────────────────────────────────────
    results.push_back(runScenario("tw_wide",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 1.0, true, 60, 120}));
    results.push_back(runScenario("tw_stress",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 1.0, true, 5, 15}));
    results.push_back(runScenario("tw_hard",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 1.0, true, 0, 3}));

    // ── Interactions ─────────────────────────────────────────────────────────
    results.push_back(runScenario("dist_tw_combo",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.5, 6000, 1.0, true, 5, 15}));

    // ── Realistic ────────────────────────────────────────────────────────────
    results.push_back(runScenario("fleet_tight",
        {42, 100,  5, LAT, LON, R, 15, 25, 5, 15, 0.4, 8000, 0.4, true, 60, 120}));
    results.push_back(runScenario("mixed",
        {42, 100, 10, LAT, LON, R, 15, 25, 5, 15, 0.4, 8000, 0.4, true, 60, 120}));
    results.push_back(runScenario("clustered_3",
        {42, 100, 10, LAT, LON, R, 15, 25, 5, 15, 0.4, 8000, 0.4,
         true, 60, 120, 3, 0.02}));

    // ── Scale ────────────────────────────────────────────────────────────────
    results.push_back(runScenario("scale_n200",
        {99, 200, 12, LAT, LON, R, 20, 35, 3, 12, 0.3, 8000, 0.4, true, 60, 120}));
    results.push_back(runScenario("scale_n400",
        { 7, 400, 15, LAT, LON, R, 25, 45, 3, 12, 0.3, 9000, 0.4, true, 60, 120}));

    printTable(results);

    std::string out = "test/bench_results.json";
    writeJSON(results, out);
    std::cout << "\nResults written to: " << out << "\n";
    return 0;
}
