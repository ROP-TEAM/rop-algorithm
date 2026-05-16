#include "routeOpt/or_opt.h"
#include "validator/route_validator.h"
#include <algorithm>
#include <limits>
#include <iostream> // เพิ่มสำหรับ log

namespace hfvrptwb {
namespace {

const double kInf = std::numeric_limits<double>::infinity();

struct NormRoute {
    int vehicle_index = -1;
    std::vector<std::vector<int>> trips;
    double cost = 0.0; // internal score 
    double billing_cost = 0.0;
};

// add a separate billing eval:
double evalBillingCost(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const std::vector<std::vector<int>>& trips,
    double billing_fc, double billing_cpk, int reload)
{
    if (trips.empty()) return 0.0;
    auto r = validateTrips(req, vehicle, trips,
        billing_fc, billing_cpk,
        billing_fc, billing_cpk, reload);
    return r.feasible ? r.total_cost : kInf;
}

double evalCost(
    const solver::SolveRequest& req,
    const solver::Vehicle& vehicle,
    const std::vector<std::vector<int>>& trips,
    double billing_fc, double billing_cpk,
    double score_fc, double score_cpk, int reload)
{
    if (trips.empty()) return 0.0;
    auto r = validateTrips(req, vehicle, trips,
        billing_fc, billing_cpk,
        score_fc, score_cpk, reload);
    return r.feasible ? r.internal_score : kInf;
}

std::vector<std::vector<int>> removeStop(
    const std::vector<std::vector<int>>& trips, int ti, int si)
{
    auto t = trips;
    if (ti < 0 || ti >= (int)t.size()) {
        std::cout << "[ERROR] removeStop invalid ti" << std::endl;
        return t;
    }
    if (si < 0 || si >= (int)t[ti].size()) {
        std::cout << "[ERROR] removeStop invalid si" << std::endl;
        return t;
    }

    t[ti].erase(t[ti].begin() + si);
    if (t[ti].empty()) {
        // std::cout << "[INFO] trip " << ti << " became empty, erasing" << std::endl;
        t.erase(t.begin() + ti);
    }
    return t;
}

std::vector<std::vector<int>> insertStop(
    const std::vector<std::vector<int>>& trips, int node, int ti, int si)
{
    auto t = trips;
    if (ti == (int)t.size()) {
        if (si != 0) {
            std::cout << "[ERROR] insertStop invalid insert position" << std::endl;
            return t;
        }
        t.push_back({});
        // std::cout << "[INFO] insertStop created new trip at index " << ti << std::endl;
    }
    if (ti < 0 || ti >= (int)t.size()) {
        std::cout << "[ERROR] insertStop invalid ti after push" << std::endl;
        return t;
    }
    if (si < 0 || si > (int)t[ti].size()) {
        std::cout << "[ERROR] insertStop invalid si" << std::endl;
        return t;
    }

    t[ti].insert(t[ti].begin() + si, node);
    return t;
}

} // namespace

void orOptRelocate(
    ConstructionResult& plan,
    const solver::SolveRequest& req,
    const SolveConfig& cfg)
{
    std::vector<NormRoute> routes;
    routes.reserve(plan.routes.size());
    for (auto& r : plan.routes) {
        NormRoute nr;
        nr.vehicle_index = r.vehicle_index;
        nr.trips = r.trips.empty()
            ? std::vector<std::vector<int>>{r.nodes}
            : r.trips;
        nr.cost = evalCost(req, req.vehicles(r.vehicle_index), nr.trips,
                           cfg.fixedCostPerVehicle, cfg.costPerKm, 
                           cfg.weight_fixed_cost, cfg.weight_per_km, cfg.reloadMin);
        nr.billing_cost = evalBillingCost(req, req.vehicles(r.vehicle_index), nr.trips,
            cfg.fixedCostPerVehicle, cfg.costPerKm, cfg.reloadMin);
        routes.push_back(std::move(nr));
    }

    bool improved = true;
    while (improved) {
        improved = false;
        for (int ai = 0; ai < (int)routes.size() && !improved; ++ai) {
            auto& src = routes[ai];
            const auto& sv = req.vehicles(src.vehicle_index);

            auto currentTrips = src.trips;
            for (int ti = 0; ti < (int)currentTrips.size() && !improved; ++ti) {
                for (int si = 0; si < (int)currentTrips[ti].size() && !improved; ++si) {
                    int node = currentTrips[ti][si];

                    // ลบ stop ออกจาก snapshot
                    auto src_after = removeStop(currentTrips, ti, si);
                    double src_cost_after = evalCost(req, sv, src_after,
                                                    cfg.fixedCostPerVehicle, cfg.costPerKm,
                                                    cfg.weight_fixed_cost,
                                                    cfg.weight_per_km, cfg.reloadMin);
                    if (!std::isfinite(src_cost_after)) continue;
                    double removal_gain = src.cost - src_cost_after;

                    for (int bi = 0; bi < (int)routes.size() && !improved; ++bi) {
                        auto& dst = routes[bi];
                        const auto& dv = req.vehicles(dst.vehicle_index);
                        const auto& base_trips = (ai == bi) ? src_after : dst.trips;
                        double base_cost = (ai == bi) ? src_cost_after : dst.cost;
                        if (!std::isfinite(base_cost)) continue;

                        int trip_slots = (int)base_trips.size()
                            + (cfg.enableMultiTrip && ai != bi ? 1 : 0);

                        // ใช้ src_after เช็คว่า trip index ยังอยู่จริง
                        bool sameVehicleTripSurvived = (ai == bi) && (ti < (int)src_after.size());

                        for (int tj = 0; tj < trip_slots && !improved; ++tj) {
                            int pos_end = tj < (int)base_trips.size()
                                ? (int)base_trips[tj].size() + 1
                                : 1;

                            for (int pj = 0; pj < pos_end && !improved; ++pj) {
                                if (sameVehicleTripSurvived && tj == ti && pj == si) continue;

                                auto dst_after = insertStop(base_trips, node, tj, pj);
                                double dst_cost_after = evalCost(req, dv, dst_after,
                                                                cfg.fixedCostPerVehicle, cfg.costPerKm,
                                                                cfg.weight_fixed_cost,
                                                                cfg.weight_per_km,
                                                                cfg.reloadMin);
                                if (!std::isfinite(dst_cost_after)) continue;

                                double net_gain = removal_gain - (dst_cost_after - base_cost);
                                if (net_gain > 1e-6) {
                                  src.trips = src_after;
                                  src.cost  = src_cost_after;
                                  src.billing_cost = evalBillingCost(req, sv, src_after,
                                      cfg.fixedCostPerVehicle, cfg.costPerKm, cfg.reloadMin);
                                  dst.trips = dst_after;
                                  dst.cost  = dst_cost_after;
                                  dst.billing_cost = evalBillingCost(req, dv, dst_after,
                                      cfg.fixedCostPerVehicle, cfg.costPerKm, cfg.reloadMin);
                                  improved  = true;
                                }
                            }
                        }
                    }
                }
            }
        }
    }

    plan.routes.clear();
    for (const auto& nr : routes) {
        if (nr.trips.empty()) continue;
        ConstructedRoute r;
        r.vehicle_index = nr.vehicle_index;
        bool single = nr.trips.size() == 1;
        if (single) {
            r.nodes = nr.trips[0];
        } else {
            r.trips = nr.trips;
            for (const auto& t : nr.trips)
                r.nodes.insert(r.nodes.end(), t.begin(), t.end());
        }
        r.total_cost = nr.billing_cost;
        r.internal_score = nr.cost;
        plan.routes.push_back(std::move(r));
    }
}
} // namespace hfvrptwb
