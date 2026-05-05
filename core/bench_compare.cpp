// bench_compare.cpp — run V1 and V2 on identical scenarios, print delta table
#include "bench_gen.h"
#include "solver_service.h"
#include <iostream>
#include <iomanip>
#include <fstream>
#include <filesystem>
#include <chrono>
#include <functional>
#include <vector>
#include <string>
#include <tuple>

using SolveRequest  = solver::SolveRequest;
using SolveResponse = solver::SolveResponse;
using SolveFn       = std::function<SolveResponse(const SolveRequest&)>;

struct RouteStop {
    std::string node_id;
    int         arrival_min;
    int         depart_min;
};

struct RouteLog {
    std::string            vehicle_id;
    int                    stops_count;
    double                 distance_m;
    int                    duration_min;
    std::vector<RouteStop> stops;
};

struct SolverResult {
    std::string            status;
    int                    assigned;
    double                 objective;
    double                 elapsed_ms;
    std::vector<RouteLog>  routes;
    std::vector<std::string> unassigned;
};

struct CompareResult {
    std::string  name;
    int          num_orders;
    int          num_vehicles;
    SolverResult v1;
    SolverResult v2;
};

static SolverResult measureSolver(const SolveFn& fn,
                                  const SolveRequest& req, int total_orders) {
    auto t0  = std::chrono::steady_clock::now();
    auto res = fn(req);
    double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
    SolverResult r;
    r.status     = res.status();
    r.assigned   = total_orders - res.unassigned_size();
    r.objective  = res.objective();
    r.elapsed_ms = ms;
    for (const auto& route : res.routes()) {
        RouteLog rl;
        rl.vehicle_id   = route.vehicle_id();
        rl.stops_count  = route.stops_size();
        rl.distance_m   = route.total_distance();
        rl.duration_min = route.total_duration();
        for (const auto& s : route.stops())
            rl.stops.push_back({s.node_id(), s.arrival_min(), s.depart_min()});
        r.routes.push_back(std::move(rl));
    }
    for (const auto& id : res.unassigned()) r.unassigned.push_back(id);
    return r;
}

static CompareResult runScenario(const char* name, const BenchConfig& cfg,
                                 const SolveFn& fn_v1, const SolveFn& fn_v2) {
    auto req = buildRequest(cfg);
    return {name, cfg.num_orders, cfg.num_vehicles,
            measureSolver(fn_v1, req, cfg.num_orders),
            measureSolver(fn_v2, req, cfg.num_orders)};
}

static void printTable(const std::vector<CompareResult>& results) {
    std::cout << std::left
              << std::setw(18) << "Scenario" << std::setw(6) << "N"
              << std::setw(10) << "Assigned"
              << std::setw(12) << "Obj_V1" << std::setw(12) << "Obj_V2"
              << std::setw(9)  << "Dobj%"
              << std::setw(10) << "ms_V1"  << std::setw(10) << "ms_V2"
              << std::setw(9)  << "Dms%"
              << "\n" << std::string(96, '-') << "\n";
    for (const auto& r : results) {
        double obj_delta = (r.v1.objective > 0)
            ? (r.v2.objective - r.v1.objective) / r.v1.objective * 100.0 : 0.0;
        double ms_delta = (r.v1.elapsed_ms > 0)
            ? (r.v2.elapsed_ms - r.v1.elapsed_ms) / r.v1.elapsed_ms * 100.0 : 0.0;
        std::cout << std::left
                  << std::setw(18) << r.name << std::setw(6) << r.num_orders
                  << std::setw(10) << (std::to_string(r.v1.assigned) + "/" +
                                       std::to_string(r.num_orders))
                  << std::fixed << std::setprecision(0)
                  << std::setw(12) << r.v1.objective << std::setw(12) << r.v2.objective
                  << std::setprecision(1)
                  << std::setw(9) << obj_delta << std::setw(10) << r.v1.elapsed_ms
                  << std::setw(10) << r.v2.elapsed_ms << std::setw(9) << ms_delta
                  << "\n";
    }
}

static void writeSolverResult(std::ofstream& f, const SolverResult& r) {
    f << "{\n"
      << "      \"status\":\"" << r.status << "\","
      << "\"assigned\":"       << r.assigned << ","
      << "\"objective\":"      << std::fixed << std::setprecision(2) << r.objective << ","
      << "\"elapsed_ms\":"     << std::setprecision(3) << r.elapsed_ms << ",\n"
      << "      \"routes\":[\n";
    for (int j = 0; j < (int)r.routes.size(); ++j) {
        const auto& rt = r.routes[j];
        f << "        {\"vehicle_id\":\"" << rt.vehicle_id << "\","
          << "\"stops_count\":"           << rt.stops_count << ","
          << "\"distance_m\":"            << std::setprecision(2) << rt.distance_m << ","
          << "\"duration_min\":"          << rt.duration_min << ","
          << "\"stops\":[\n";
        for (int k = 0; k < (int)rt.stops.size(); ++k) {
            const auto& s = rt.stops[k];
            f << "          {\"node_id\":\"" << s.node_id << "\","
              << "\"arrival_min\":"          << s.arrival_min << ","
              << "\"depart_min\":"           << s.depart_min << "}";
            if (k + 1 < (int)rt.stops.size()) f << ",";
            f << "\n";
        }
        f << "        ]}";
        if (j + 1 < (int)r.routes.size()) f << ",";
        f << "\n";
    }
    f << "      ],\n      \"unassigned\":[";
    for (int j = 0; j < (int)r.unassigned.size(); ++j) {
        f << "\"" << r.unassigned[j] << "\"";
        if (j + 1 < (int)r.unassigned.size()) f << ",";
    }
    f << "]\n    }";
}

static void writeJSON(const std::vector<CompareResult>& results,
                      const std::string& path) {
    std::ofstream f(path);
    f << "[\n";
    for (int i = 0; i < (int)results.size(); ++i) {
        const auto& r = results[i];
        double obj_delta = (r.v1.objective > 0)
            ? (r.v2.objective - r.v1.objective) / r.v1.objective * 100.0 : 0.0;
        double ms_delta = (r.v1.elapsed_ms > 0)
            ? (r.v2.elapsed_ms - r.v1.elapsed_ms) / r.v1.elapsed_ms * 100.0 : 0.0;
        f << "  {\n"
          << "    \"name\":\""        << r.name         << "\",\n"
          << "    \"n\":"             << r.num_orders    << ",\n"
          << "    \"v\":"             << r.num_vehicles  << ",\n"
          << "    \"obj_delta_pct\":" << std::fixed << std::setprecision(2) << obj_delta << ",\n"
          << "    \"ms_delta_pct\":"  << ms_delta << ",\n"
          << "    \"v1\":"; writeSolverResult(f, r.v1);
        f << ",\n    \"v2\":"; writeSolverResult(f, r.v2);
        f << "\n  }";
        if (i + 1 < (int)results.size()) f << ",";
        f << "\n";
    }
    f << "]\n";
}

int main() {
    const double LAT = 16.4442, LON = 102.8352, R = 0.09;
    std::filesystem::create_directories("test");

    SolveConfig cfg;
    SolverServiceImpl svc(cfg);
    SolverV2          v2(cfg);

    SolveFn fn_v1 = [&](const SolveRequest& req) {
        SolveResponse resp;
        svc.Solve(nullptr, &req, &resp);
        return resp;
    };
    SolveFn fn_v2 = [&](const SolveRequest& req) { return v2.Solve(req); };

    std::vector<CompareResult> results;
    results.push_back(runScenario("baseline",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 1.0, false, 0, 0}, fn_v1, fn_v2));

    results.push_back(runScenario("cap_loose",
        {42, 100, 10, LAT, LON, R, 40, 60, 5, 10, 0.0, 0, 1.0, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("cap_medium",
        {42, 100, 10, LAT, LON, R, 20, 30, 5, 10, 0.0, 0, 1.0, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("cap_tight",
        {42, 100, 10, LAT, LON, R, 10, 15, 5, 10, 0.0, 0, 1.0, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("cap_stress",
        {42, 100, 10, LAT, LON, R, 10, 14, 8, 18, 0.0, 0, 1.0, false, 0, 0}, fn_v1, fn_v2));

    results.push_back(runScenario("tag_tc05",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.05, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("tag_tc10",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.10, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("tag_tc20",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.20, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("tag_tc50",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.50, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("tag_stress",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.10, false, 0, 0}, fn_v1, fn_v2));

    results.push_back(runScenario("dist_sw_02",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.2, 3000, 1.0, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("dist_sw_05",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.5, 6000, 1.0, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("dist_sw_10",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 1.0, 5000, 1.0, false, 0, 0}, fn_v1, fn_v2));
    results.push_back(runScenario("dist_stress",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 1.0, 5000, 1.0, false, 0, 0}, fn_v1, fn_v2));

    results.push_back(runScenario("tw_wide",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 1.0, true, 60, 120}, fn_v1, fn_v2));
    results.push_back(runScenario("tw_stress",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 1.0, true, 5, 15}, fn_v1, fn_v2));
    results.push_back(runScenario("tw_hard",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 1.0, true, 0, 3}, fn_v1, fn_v2));

    results.push_back(runScenario("dist_tw_combo",
        {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.5, 6000, 1.0, true, 5, 15}, fn_v1, fn_v2));

    results.push_back(runScenario("fleet_tight",
        {42, 100,  5, LAT, LON, R, 15, 25, 5, 15, 0.4, 8000, 0.4, true, 60, 120}, fn_v1, fn_v2));
    results.push_back(runScenario("mixed",
        {42, 100, 10, LAT, LON, R, 15, 25, 5, 15, 0.4, 8000, 0.4, true, 60, 120}, fn_v1, fn_v2));
    results.push_back(runScenario("clustered_3",
        {42, 100, 10, LAT, LON, R, 15, 25, 5, 15, 0.4, 8000, 0.4,
         true, 60, 120, 3, 0.02}, fn_v1, fn_v2));

    results.push_back(runScenario("scale_n200",
        {99, 200, 12, LAT, LON, R, 20, 35, 3, 12, 0.3, 8000, 0.4, true, 60, 120}, fn_v1, fn_v2));
    results.push_back(runScenario("scale_n400",
        { 7, 400, 15, LAT, LON, R, 25, 45, 3, 12, 0.3, 9000, 0.4, true, 60, 120}, fn_v1, fn_v2));

    printTable(results);
    writeJSON(results, "test/bench_compare.json");
    std::cout << "\nResults written to: test/bench_compare.json\n";
    return 0;
}
