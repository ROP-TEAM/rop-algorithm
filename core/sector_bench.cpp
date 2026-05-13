// sector_bench.cpp — per-instance-type comparison: sectorRemoval on vs off
#include "bench_gen.h"
#include "solver_service.h"
#include <algorithm>
#include <chrono>
#include <cmath>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <string>
#include <vector>

struct TrialResult {
    double objective;
    double elapsed_ms;
    int assigned;
};

struct ScenarioStats {
    double mean_obj, min_obj, max_obj, stddev_obj;
    double mean_ms;
    int best_trial;
};

ScenarioStats computeStats(const std::vector<TrialResult>& results) {
    ScenarioStats s{};
    if (results.empty()) return s;
    s.min_obj = std::numeric_limits<double>::infinity();
    for (const auto& r : results) {
        s.mean_obj += r.objective;
        s.mean_ms += r.elapsed_ms;
        if (r.objective < s.min_obj) { s.min_obj = r.objective; s.best_trial = (int)(&r - &results[0]); }
        if (r.objective > s.max_obj) s.max_obj = r.objective;
    }
    double n = (double)results.size();
    s.mean_obj /= n;
    s.mean_ms /= n;
    double var = 0.0;
    for (const auto& r : results) {
        double d = r.objective - s.mean_obj;
        var += d * d;
    }
    s.stddev_obj = std::sqrt(var / n);
    return s;
}

struct ComparisonRow {
    std::string scenario;
    int n, v;
    ScenarioStats on, off;
    double dobj_pct;   // (off.mean - on.mean) / off.mean * 100 — negative = sector helps
    int wins;          // trials where sector_on <= sector_off
    int trials;
};

int main() {
    const double LAT = 16.4442, LON = 102.8352, R = 0.09;
    const int TRIALS = 10;

    struct InstanceSpec {
        const char* name;
        const char* desc;
        BenchConfig cfg;
    };

    InstanceSpec specs[] = {
        {"geo_clustered", "3 clusters, wide TW",
         {42, 100, 10, LAT, LON, R, 20, 30, 5, 10, 0.0, 0, 1.0, true, 60, 120, 3, 0.02}},
        {"geo_dispersed", "uniform, wide TW",
         {42, 100, 10, LAT, LON, R, 20, 30, 5, 10, 0.0, 0, 1.0, true, 60, 120, 0, 0}},
        {"temporal_tight", "uniform, tight TW (5-15 min)",
         {42, 100, 10, LAT, LON, R, 20, 30, 5, 10, 0.0, 0, 1.0, true, 5, 15, 0, 0}},
        {"temporal_mixed", "3 clusters, tight TW",
         {42, 100, 10, LAT, LON, R, 20, 30, 5, 10, 0.0, 0, 1.0, true, 5, 15, 3, 0.02}},
        {"fleet_tight", "5 vehicles, tight cap, wide TW",
         {42, 100,  5, LAT, LON, R, 10, 15, 5, 15, 0.4, 8000, 0.4, true, 60, 120, 0, 0}},
        {"tag_stress", "low tag coverage (10%), wide TW",
         {42, 100, 10, LAT, LON, R, 50, 80, 2, 8, 0.0, 0, 0.10, true, 60, 120, 0, 0}},
        {"scale_n200", "200 orders, 12 vehicles, mixed",
         {99, 200, 12, LAT, LON, R, 20, 35, 3, 12, 0.3, 8000, 0.4, true, 60, 120, 0, 0}},
    };

    std::vector<ComparisonRow> rows;
    std::cout << "sector_bench: comparing sectorRemoval ON vs OFF, " << TRIALS
              << " trials each\n" << std::string(60, '-') << "\n";

    for (const auto& spec : specs) {
        std::vector<TrialResult> on_results, off_results;

        for (int trial = 0; trial < TRIALS; ++trial) {
            BenchConfig bc = spec.cfg;
            bc.seed = spec.cfg.seed + trial; // same instances for both configs
            auto req = buildRequest(bc);

            // === sectorRemoval ON ===
            {
                SolveConfig sc;
                sc.enableALNS = true;
                sc.enableSectorRemoval = true;
                sc.seed = (uint32_t)(spec.cfg.seed + trial);
                SolverV2 solver(sc);
                auto t0 = std::chrono::steady_clock::now();
                auto resp = solver.Solve(req);
                double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
                on_results.push_back({resp.objective(), ms,
                    bc.num_orders - resp.unassigned_size()});
            }

            // === sectorRemoval OFF ===
            {
                SolveConfig sc;
                sc.enableALNS = true;
                sc.enableSectorRemoval = false;
                sc.seed = (uint32_t)(spec.cfg.seed + trial);
                SolverV2 solver(sc);
                auto t0 = std::chrono::steady_clock::now();
                auto resp = solver.Solve(req);
                double ms = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - t0).count();
                off_results.push_back({resp.objective(), ms,
                    bc.num_orders - resp.unassigned_size()});
            }
        }

        ComparisonRow row;
        row.scenario = spec.name;
        row.n = spec.cfg.num_orders;
        row.v = spec.cfg.num_vehicles;
        row.on = computeStats(on_results);
        row.off = computeStats(off_results);
        row.trials = TRIALS;

        row.dobj_pct = (row.off.mean_obj > 0)
            ? (row.on.mean_obj - row.off.mean_obj) / row.off.mean_obj * 100.0 : 0.0;

        row.wins = 0;
        for (int i = 0; i < TRIALS; ++i) {
            if (on_results[i].objective <= off_results[i].objective) ++row.wins;
        }

        rows.push_back(row);

        std::cout << std::setw(18) << std::left << spec.name
                  << " | on_mean="  << std::fixed << std::setprecision(0) << std::setw(10) << row.on.mean_obj
                  << " off_mean="   << std::setw(10) << row.off.mean_obj
                  << " dobj="       << std::setprecision(1) << std::setw(6) << row.dobj_pct << "%"
                  << " win="        << row.wins << "/" << TRIALS
                  << " std_on="     << std::setprecision(0) << std::setw(8) << row.on.stddev_obj
                  << " std_off="    << std::setw(8) << row.off.stddev_obj
                  << " ms_on="      << std::setprecision(0) << std::setw(6) << row.on.mean_ms
                  << " ms_off="     << std::setw(6) << row.off.mean_ms
                  << " " << spec.desc << "\n";
    }

    // Summary table
    std::cout << "\n" << std::string(90, '-') << "\n"
              << std::left
              << std::setw(18) << "Scenario"
              << std::setw(6) << "N"
              << std::setw(4) << "V"
              << std::setw(12) << "Obj_ON"
              << std::setw(12) << "Obj_OFF"
              << std::setw(8) << "Dobj%"
              << std::setw(8) << "WinRate"
              << std::setw(10) << "StdDiv_ON"
              << std::setw(10) << "StdDiv_OFF"
              << std::setw(10) << "ms_ON"
              << std::setw(10) << "ms_OFF"
              << "Type\n"
              << std::string(90, '-') << "\n";

    for (const auto& r : rows) {
        std::cout << std::left
                  << std::setw(18) << r.scenario
                  << std::setw(6)  << r.n
                  << std::setw(4)  << r.v
                  << std::fixed << std::setprecision(0)
                  << std::setw(12) << r.on.mean_obj
                  << std::setw(12) << r.off.mean_obj
                  << std::setprecision(1)
                  << std::setw(8)  << r.dobj_pct << "%"
                  << std::setw(8)  << (std::to_string(r.wins) + "/" + std::to_string(r.trials))
                  << std::setprecision(0)
                  << std::setw(10) << r.on.stddev_obj
                  << std::setw(10) << r.off.stddev_obj
                  << std::setprecision(0)
                  << std::setw(10) << r.on.mean_ms
                  << std::setw(10) << r.off.mean_ms;
        // Interpret result
        if (r.dobj_pct < -1.0)
            std::cout << "sector HELPS";
        else if (r.dobj_pct > 1.0)
            std::cout << "sector HURTS";
        else
            std::cout << "neutral";
        std::cout << "\n";
    }
    std::cout << "\nNegative Dobj% = sectorRemoval ON has lower (better) objective than OFF\n";

    // ── Operator weight breakdown per scenario type ─────────────────────────
    std::cout << "\n" << std::string(100, '=') << "\n"
              << "Operator weight breakdown (sectorRemoval ON, 1 trial each)\n"
              << std::string(100, '-') << "\n"
              << std::left
              << std::setw(18) << "Scenario"
              << std::setw(14) << "Operator"
              << std::setw(10) << "Weight"
              << std::setw(10) << "Selected"
              << std::setw(12) << "Improved"
              << std::setw(10) << "Best"
              << "Effective?\n"
              << std::string(100, '-') << "\n";

    for (const auto& spec : specs) {
        BenchConfig bc = spec.cfg;
        bc.seed = spec.cfg.seed;
        auto req = buildRequest(bc);

        SolveConfig sc;
        sc.enableALNS = true;
        sc.enableSectorRemoval = true;
        sc.seed = (uint32_t)spec.cfg.seed;
        SolverV2 solver(sc);
        solver.Solve(req);

        const auto& st = solver.lastAlnsStats();
        int ND = (int)st.final_weights.size();
        int total_sel = 0;
        for (int d = 0; d < ND; ++d) total_sel += st.selection_count[d];

        for (int d = 0; d < ND; ++d) {
            double pct = total_sel > 0 ? 100.0 * st.selection_count[d] / total_sel : 0.0;
            bool effective = st.selection_count[d] > 0
                && (double)st.improve_count[d] / st.selection_count[d] > 0.3;

            std::cout << std::left
                      << std::setw(18) << (d == 0 ? spec.name : "")
                      << std::setw(14) << hfvrptwb::alns::destroyOperatorName(d)
                      << std::fixed << std::setprecision(2)
                      << std::setw(10) << st.final_weights[d]
                      << std::setprecision(0)
                      << std::setw(10) << (std::to_string(st.selection_count[d]) +
                                           " (" + std::to_string((int)pct) + "%)")
                      << std::setw(12) << (std::to_string(st.improve_count[d]) +
                                           "/" + std::to_string(st.selection_count[d]))
                      << std::setw(10) << st.best_count[d]
                      << (effective ? "yes" : "")
                      << "\n";
        }
        std::cout << std::string(100, '-') << "\n";
    }

    return 0;
}
