#include "alns/alns.h"
#include "alns/destroy.h"
#include "alns/repair.h"
#include "alns/local_search.h"
#include "construction/adaptive_constructor.h"
#include "routeOpt/two_opt.h"
#include "validator/route_validator.h"
#include <functional>
#include <iostream>
#include <random>

namespace hfvrptwb {
namespace alns {

using DestroyFn = std::function<void(ALNSSolution&, std::mt19937&,
    const solver::SolveRequest&, double, double, int)>;
using RepairFn = std::function<void(ALNSSolution&,
    const solver::SolveRequest&, double, double, int)>;

static void applyTwoOptToSolution(ALNSSolution& sol,
    const solver::SolveRequest& req, double fixed, double km)
{
    for (auto& vt : sol.vehicles) {
        const auto& vehicle = req.vehicles(vt.vehicle_index);

        // Save originals for rollback
        auto original_trips = vt.trips;

        for (auto& trip : vt.trips) {
            auto opt = twoOptTrip(req, vehicle, trip, fixed, km);
            if (std::isfinite(opt.cost)) {
                trip = std::move(opt);
            }
        }

        // Validate full schedule stays feasible
        std::vector<std::vector<int>> all_trips;
        for (const auto& t : vt.trips) all_trips.push_back(t.nodes);
        auto val = validateTrips(req, vehicle, all_trips, fixed, km, 0);
        if (!val.feasible || !std::isfinite(val.total_cost)) {
            vt.trips = std::move(original_trips);
        }
    }
    sol.objective = computeObjective(sol.vehicles, (int)sol.unrouted.size());
}

ConstructionResult ALNSSolver::solve(
    const solver::SolveRequest& req,
    const ConstructionResult& initial,
    double fixed, double km,
    std::chrono::milliseconds budget,
    int reload_min,
    uint32_t seed)
{
    auto current = fromConstruction(initial, req, fixed, km);
    auto best = current;
    double best_obj = best.objective;

    std::mt19937 rng(seed);

    // Operator function tables
    DestroyFn destroyers[] = {
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n) { randomRemoval(s, r, q, f, k, n); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n) { worstRemoval(s, r, q, f, k, n); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n) { shawRemoval(s, r, q, f, k, n); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n) { priorityAwareRemoval(s, r, q, f, k, n); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n) { routeConsolidationDestroy(s, r, q, f, k, n); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n) { tripRemoval(s, r, q, f, k, n); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n) { tagViolationRemoval(s, r, q, f, k, n); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n) { lateCustomerRemoval(s, r, q, f, k, n); },
    };

    RepairFn repairers[] = {
        greedyRepair,
        priorityFirstRepair,
        regret2Repair,
        proactiveBreakInsertion,
    };

    constexpr int ND = 8;
    constexpr int NR = 4;

    std::vector<double> destroy_weights(ND, 1.0);
    std::vector<double> repair_weights(NR, 1.0);

    double T = cfg_.initial_temp;
    int seg_feasible = 0;
    int seg_total = 0;
    int iter = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    AdaptivePenalty penalty;
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t0);
        if (elapsed >= budget) break;

        // Pick operators by weighted random
        std::discrete_distribution<int> d_dist(destroy_weights.begin(), destroy_weights.end());
        std::discrete_distribution<int> r_dist(repair_weights.begin(), repair_weights.end());
        int d = d_dist(rng);
        int r = r_dist(rng);

        // Destroy + repair
        ALNSSolution candidate = current;
        int q = std::max(1, std::min(8, (int)candidate.vehicles.size()));
        destroyers[d](candidate, rng, req, fixed, km, q);
        repairers[r](candidate, req, fixed, km, reload_min);
        applyTwoOptToSolution(candidate, req, fixed, km);

        // Acceptance
        double delta = candidate.objective - current.objective;
        bool accept = delta < 0 || uni(rng) < std::exp(-delta / T);

        int score = 0;
        if (accept) {
            current = std::move(candidate);
            if (current.unrouted.empty()) ++seg_feasible;
            ++seg_total;

            if (current.objective < best_obj) {
                best = current;
                best_obj = current.objective;
                score = cfg_.score_best;
            } else if (delta < 0) {
                score = cfg_.score_better;
            } else {
                score = cfg_.score_accepted;
            }
        }

        // Update weights per segment
        ++iter;
        if (iter % cfg_.segment_size == 0) {
            double r_factor = cfg_.reaction_factor;
            destroy_weights[d] = (1.0 - r_factor) * destroy_weights[d] + r_factor * score;
            repair_weights[r]  = (1.0 - r_factor) * repair_weights[r]  + r_factor * score;

            if (seg_total > 0) {
                penalty.update((double)seg_feasible / (double)seg_total);
            }
            seg_feasible = 0;
            seg_total = 0;
        }

        T *= cfg_.cooling_rate;
        if (T < 0.01) T = cfg_.initial_temp; // restart if too cold
    }

    applyTwoOptToSolution(best, req, fixed, km);

    return toConstruction(best);
}

} // namespace alns
} // namespace hfvrptwb
