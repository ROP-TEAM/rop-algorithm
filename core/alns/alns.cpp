#include "alns/alns.h"
#include "alns/destroy.h"
#include "alns/repair.h"
#include "alns/local_search.h"
#include "construction/adaptive_constructor.h"
#include "routeOpt/two_opt.h"
#include "validator/route_validator.h"
#include <algorithm>
#include <functional>
#include <iostream>
#include <limits>
#include <random>
#include <cmath>

namespace hfvrptwb {
namespace alns {

// ← added weight_wait_time to both function type signatures
using DestroyFn = std::function<void(ALNSSolution&, std::mt19937&,
    const solver::SolveRequest&, double, double, int, double)>;
using RepairFn = std::function<void(ALNSSolution&,
    const solver::SolveRequest&, double, double, int, double)>;

static void applyTwoOptToSolution(ALNSSolution& sol,
    const solver::SolveRequest& req, double fixed, double km,
    double weight_wait_time = 0.0)  // ← add weight_wait_time
{
    for (auto& vt : sol.vehicles) {
        const auto& vehicle = req.vehicles(vt.vehicle_index);

        auto original_trips = vt.trips;

        for (auto& trip : vt.trips) {
            auto opt = twoOptTrip(req, vehicle, trip, fixed, km);
            if (std::isfinite(opt.cost)) {
                trip = std::move(opt);
            }
        }

        std::vector<std::vector<int>> all_trips;
        for (const auto& t : vt.trips) all_trips.push_back(t.nodes);
        // ← pass weight_wait_time to validateTrips
        auto val = validateTrips(req, vehicle, all_trips,
                                 fixed, km, fixed, km, 0, weight_wait_time);
        if (!val.feasible || !std::isfinite(val.total_cost)) {
            vt.trips = std::move(original_trips);
        }
    }
    sol.objective       = computeObjective(sol.vehicles, (int)sol.unrouted.size());
    sol.internal_score  = computeInternalScore(sol.vehicles, (int)sol.unrouted.size());
}

ConstructionResult ALNSSolver::solve(
    const solver::SolveRequest& req,
    const ConstructionResult& initial,
    double fixed, double km,
    std::chrono::milliseconds budget,
    int reload_min,
    uint32_t seed,
    double weight_wait_time)
{
    auto current = fromConstruction(initial, req, fixed, km, weight_wait_time);
    auto best    = current;

    double best_obj = best.internal_score;

    ALNSSolution best_feasible;
    double best_feasible_obj = std::numeric_limits<double>::infinity();
    bool has_feasible = current.unrouted.empty();
    if (has_feasible) {
        best_feasible     = current;
        best_feasible_obj = current.internal_score;
    }

    std::mt19937 rng(seed);

    // ← lambdas now accept and forward weight_wait_time (last param)
    std::vector<DestroyFn> destroyers = {
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n, double w) { randomRemoval(s, r, q, f, k, n, w); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n, double w) { worstRemoval(s, r, q, f, k, n, w); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n, double w) { shawRemoval(s, r, q, f, k, n, w); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n, double w) { priorityAwareRemoval(s, r, q, f, k, n, w); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n, double w) { routeConsolidationDestroy(s, r, q, f, k, n, w); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n, double w) { tripRemoval(s, r, q, f, k, n, w); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n, double w) { tagViolationRemoval(s, r, q, f, k, n, w); },
        [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
           double f, double k, int n, double w) { lateCustomerRemoval(s, r, q, f, k, n, w); },
    };
    if (cfg_.enable_sector_removal) {
        destroyers.push_back(
            [](ALNSSolution& s, std::mt19937& r, const solver::SolveRequest& q,
               double f, double k, int n, double w) { sectorRemoval(s, r, q, f, k, n, w); });
    }

    // ← repair functions also get weight_wait_time
    RepairFn repairers[] = {
        [](ALNSSolution& s, const solver::SolveRequest& q,
           double f, double k, int rl, double w) { greedyRepair(s, q, f, k, rl, w); },
        [](ALNSSolution& s, const solver::SolveRequest& q,
           double f, double k, int rl, double w) { priorityFirstRepair(s, q, f, k, rl, w); },
        [](ALNSSolution& s, const solver::SolveRequest& q,
           double f, double k, int rl, double w) { regret2Repair(s, q, f, k, rl, w); },
        [](ALNSSolution& s, const solver::SolveRequest& q,
           double f, double k, int rl, double w) { regret3Repair(s, q, f, k, rl, w); },
        [](ALNSSolution& s, const solver::SolveRequest& q,
           double f, double k, int rl, double w) { proactiveBreakInsertion(s, q, f, k, rl, w); },
    };

    const int ND = (int)destroyers.size();
    const int NR = 5;

    std::vector<double> destroy_weights(ND, 1.0);
    std::vector<double> repair_weights(NR, 1.0);
    std::vector<double> destroy_score_sum(ND, 0.0);
    std::vector<double> repair_score_sum(NR, 0.0);
    std::vector<int>    destroy_use(ND, 0);
    std::vector<int>    repair_use(NR, 0);

    std::vector<int> destroy_selections(ND, 0);
    std::vector<int> destroy_improvements(ND, 0);
    std::vector<int> destroy_bests(ND, 0);

    double T = cfg_.initial_temp;
    int seg_feasible = 0;
    int seg_total    = 0;
    int iter         = 0;

    auto t0 = std::chrono::high_resolution_clock::now();
    AdaptivePenalty penalty;
    std::uniform_real_distribution<double> uni(0.0, 1.0);

    while (true) {
        auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::high_resolution_clock::now() - t0);
        if (elapsed >= budget) break;

        std::discrete_distribution<int> d_dist(destroy_weights.begin(), destroy_weights.end());
        std::discrete_distribution<int> r_dist(repair_weights.begin(), repair_weights.end());
        int d = d_dist(rng);
        int r = r_dist(rng);
        ++destroy_selections[d];

        ALNSSolution candidate = current;
        int total_routed = 0;
        for (const auto& vt : candidate.vehicles)
            for (const auto& t : vt.trips)
                total_routed += (int)t.nodes.size();
        int q = std::clamp(total_routed / 4, 4, 15);

        // ← pass weight_wait_time to destroy and repair
        destroyers[d](candidate, rng, req, fixed, km, q, weight_wait_time);
        candidate.forbid_new_vehicle = (d == 4) || (uni(rng) < cfg_.forbid_new_vehicle_prob);
        repairers[r](candidate, req, fixed, km, reload_min, weight_wait_time);

        candidate.objective     = computeObjective(candidate.vehicles, (int)candidate.unrouted.size(), penalty.coefficient());
        candidate.internal_score = computeInternalScore(candidate.vehicles, (int)candidate.unrouted.size(), penalty.coefficient());

        double delta = candidate.internal_score - current.internal_score;
        bool accept  = delta < 0 || uni(rng) < std::exp(-delta / T);

        int score = 0;
        if (accept) {
            current = std::move(candidate);
            if (current.unrouted.empty()) ++seg_feasible;
            ++seg_total;

            if (current.internal_score < best_obj) {
                best     = current;
                best_obj = current.internal_score;
                score    = cfg_.score_best;
            } else if (delta < 0) {
                score = cfg_.score_better;
            } else {
                score = cfg_.score_accepted;
            }

            if (current.unrouted.empty() && current.internal_score < best_feasible_obj) {
                best_feasible     = current;
                best_feasible_obj = current.internal_score;
                has_feasible      = true;
            }

            destroy_score_sum[d] += score;
            repair_score_sum[r]  += score;
            ++destroy_use[d];
            ++repair_use[r];

            if (delta < 0)                   ++destroy_improvements[d];
            if (score == cfg_.score_best)    ++destroy_bests[d];
        }

        ++iter;
        if (iter % cfg_.segment_size == 0) {
            double r_factor = cfg_.reaction_factor;
            for (int i = 0; i < ND; ++i) {
                if (destroy_use[i] > 0) {
                    double avg = destroy_score_sum[i] / destroy_use[i];
                    destroy_weights[i] = (1.0 - r_factor) * destroy_weights[i] + r_factor * avg;
                }
            }
            for (int i = 0; i < NR; ++i) {
                if (repair_use[i] > 0) {
                    double avg = repair_score_sum[i] / repair_use[i];
                    repair_weights[i] = (1.0 - r_factor) * repair_weights[i] + r_factor * avg;
                }
            }
            std::fill(destroy_score_sum.begin(), destroy_score_sum.end(), 0.0);
            std::fill(repair_score_sum.begin(),  repair_score_sum.end(),  0.0);
            std::fill(destroy_use.begin(),       destroy_use.end(),       0);
            std::fill(repair_use.begin(),        repair_use.end(),        0);

            if (seg_total > 0)
                penalty.update((double)seg_feasible / (double)seg_total);
            seg_feasible = 0;
            seg_total    = 0;
        }

        T *= cfg_.cooling_rate;
        if (T < cfg_.min_temp) T = cfg_.reheat_temp;
    }

    ALNSSolution& returned = has_feasible ? best_feasible : best;
    stats_ = {destroy_weights, destroy_selections, destroy_improvements, destroy_bests};

    returned.objective = computeObjective(returned.vehicles, (int)returned.unrouted.size());

    applyTwoOptToSolution(returned, req, fixed, km, weight_wait_time);

    return toConstruction(returned);
}

const char* destroyOperatorName(int index) {
    static const char* names[] = {
        "random", "worst", "shaw", "prioAware",
        "routeConsolidate", "tripRemove", "tagViolation",
        "lateCustomer", "sector",
    };
    if (index < 0 || index >= 9) return "???";
    return names[index];
}

} // namespace alns
} // namespace hfvrptwb
