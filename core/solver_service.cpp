#include "solver_service.h"

#include "alns/alns.h"
#include "construction/adaptive_constructor.h"
#include "drop/drop_logic.h"
#include "routeOpt/or_opt.h"
#include "routeOpt/two_opt.h"
#include "validator/route_state.h"
#include "validator/route_validator.h"
#include <chrono>
#include <iostream>
#include <unordered_set>

namespace {

void addDrop(solver::SolveResponse* resp,
             const std::string& node_id,
             const std::string& code,
             const std::string& detail)
{
    resp->add_unassigned(node_id);
    auto* dr = resp->add_drop_reasons();
    dr->set_node_id(node_id);
    dr->set_code(hfvrptwb::normalizeDropCode(code));
    dr->set_detail(detail);
}

bool writeRoute(const solver::SolveRequest& req,
                const solver::Vehicle& vehicle,
                const hfvrptwb::ConstructedRoute& constructed,
                const SolveConfig& cfg,
                solver::SolveResponse* resp)
{
    std::vector<std::vector<int>> trips = constructed.trips.empty()
        ? std::vector<std::vector<int>>{constructed.nodes}
        : constructed.trips;
    auto validation = hfvrptwb::validateTrips(
        req, vehicle, trips, cfg.fixedCostPerVehicle, cfg.costPerKm, cfg.reloadMin);
    if (!validation.feasible) {
        for (int node_index : constructed.nodes) {
            const auto& node = req.nodes(node_index - 1);
            addDrop(resp, node.id(), validation.code, validation.detail);
        }
        return false;
    }

    auto* route = resp->add_routes();
    route->set_vehicle_id(vehicle.id());
    route->set_total_distance(validation.total_distance_m);
    route->set_total_duration(validation.total_duration_min);
    route->set_total_cost(validation.total_cost);
    for (const auto& trip : trips) {
        route->add_trip_sizes((int)trip.size());
    }

    for (const auto& timing : validation.timings) {
        const auto& node = req.nodes(timing.node_index - 1);
        auto* stop = route->add_stops();
        stop->set_node_id(node.id());
        stop->set_arrival_min(timing.arrival_min);
        stop->set_depart_min(timing.depart_min);
    }

    return true;
}

void writeResponse(const solver::SolveRequest& req,
                   const SolveConfig& cfg,
                   const hfvrptwb::ConstructionResult& plan,
                   solver::SolveResponse* resp)
{
    std::unordered_set<std::string> dropped;
    for (const auto& drop : plan.drops) {
        if (dropped.insert(drop.node_id).second) {
            addDrop(resp, drop.node_id, drop.code, drop.detail);
        }
    }

    double objective = 0.0;
    for (const auto& constructed : plan.routes) {
        if (constructed.vehicle_index < 0 || constructed.vehicle_index >= req.vehicles_size()) {
            continue;
        }
        int before = resp->routes_size();
        if (writeRoute(req, req.vehicles(constructed.vehicle_index), constructed, cfg, resp)) {
            objective += resp->routes(before).total_cost();
        }
    }

    objective += 1000.0 * resp->unassigned_size();
    resp->set_objective(objective);

    if (plan.timed_out) {
        resp->set_status("TIMEOUT");
    } else if (resp->routes_size() == 0 && resp->unassigned_size() > 0) {
        resp->set_status("INFEASIBLE");
    } else {
        resp->set_status("OK");
    }
}

void applyTwoOpt(hfvrptwb::ConstructionResult& plan,
                 const solver::SolveRequest& req,
                 const SolveConfig& cfg)
{
    for (auto& route : plan.routes) {
        const auto& vehicle = req.vehicles(route.vehicle_index);

        // เก็บสำเนาเดิมไว้ใช้ fallback
        auto original_nodes = route.nodes;
        auto original_trips = route.trips;
        double original_total_cost = route.total_cost;

        bool single = route.trips.empty();
        if (single && !route.nodes.empty()) {
            // push เฉพาะกรณีมี nodes จริง
            route.trips.push_back(route.nodes);
        }

        double new_cost = 0.0;
        bool any_infeasible = false;

        for (auto& trip_nodes : route.trips) {
            if (trip_nodes.empty()) continue; // ข้าม trip ว่าง

            hfvrptwb::RouteState init;
            init.vehicle_index = route.vehicle_index;

            auto seed = hfvrptwb::evaluateRouteState(
                req, vehicle, init, trip_nodes,
                cfg.fixedCostPerVehicle, cfg.costPerKm);

            if (!seed.feasible || !std::isfinite(seed.next.cost)) {
                any_infeasible = true;
                continue;
            }

            auto opt = hfvrptwb::twoOptTrip(
                req, vehicle, std::move(seed.next),
                cfg.fixedCostPerVehicle, cfg.costPerKm);

            if (!std::isfinite(opt.cost)) {
                any_infeasible = true;
                continue;
            }

            trip_nodes = opt.nodes;
            new_cost += opt.cost;
        }

        if (any_infeasible) {
            // fallback กลับไปใช้ route เดิม
            route.nodes = std::move(original_nodes);
            route.trips = std::move(original_trips);
            route.total_cost = original_total_cost;
        } else {
            // rebuild nodes และอัพเดท cost
            if (single) {
                if (!route.trips.empty())
                    route.nodes = route.trips[0];
                else
                    route.nodes.clear();
                route.trips.clear();
            } else {
                route.nodes.clear();
                for (const auto& t : route.trips)
                    route.nodes.insert(route.nodes.end(), t.begin(), t.end());
            }
            route.total_cost = new_cost;
        }
    }
}

grpc::Status solveRequest(const solver::SolveRequest* req,
                          const SolveConfig& cfg,
                          solver::SolveResponse* resp,
                          hfvrptwb::alns::OperatorStats* out_stats = nullptr)
{
    using Clock = std::chrono::high_resolution_clock;
    auto t0 = Clock::now();

    if (req->matrix_size() < 2 || req->nodes_size() == 0) {
        resp->set_status("OK");
        return grpc::Status::OK;
    }
    if (req->vehicles_size() == 0) {
        for (const auto& node : req->nodes()) {
            addDrop(resp, node.id(), "NO_VEHICLE", "no vehicles available");
        }
        resp->set_objective(1000.0 * resp->unassigned_size());
        resp->set_status("INFEASIBLE");
        return grpc::Status::OK;
    }

    auto planObj = [](const hfvrptwb::ConstructionResult& p) -> double {
        double obj = 0.0;
        for (const auto& r : p.routes) obj += r.total_cost;
        obj += 1000.0 * (double)p.drops.size();
        return obj;
    };

    auto plan = hfvrptwb::adaptiveConstruct(
        *req, cfg.fixedCostPerVehicle, cfg.costPerKm, cfg.enableMultiTrip, cfg.reloadMin);
    auto t1 = Clock::now();
    std::cout << "[Phase] construction"
              << "  obj=" << planObj(plan)
              << "  routes=" << plan.routes.size()
              << "  unassigned=" << plan.drops.size()
              << "  ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count() << "\n";

    // Post-construction config: ALNS doesn't account for reload breaks between trips,
    // so all downstream phases use reloadMin=0 when ALNS is active.
    SolveConfig post_cfg = cfg;
    if (cfg.enableALNS) post_cfg.reloadMin = 0;

    if (cfg.enableALNS) {
        int limit_ms = req->time_limit_ms() > 0 ? req->time_limit_ms() : 5000;
        int construction_ms = (int)std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count();
        int alns_budget_ms = std::max(100, limit_ms - construction_ms);

        hfvrptwb::alns::ALNSConfig alns_cfg;
        alns_cfg.enable_sector_removal = cfg.enableSectorRemoval;

        int n_starts = std::max(1, cfg.multiStartCount);
        auto budget_per_start = std::chrono::milliseconds(alns_budget_ms / n_starts);

        auto best_plan = plan;
        double best_obj = planObj(plan);

        for (int s = 0; s < n_starts; ++s) {
            uint32_t run_seed = cfg.seed + s * 31337;
            hfvrptwb::alns::ALNSSolver alns_solver(alns_cfg);
            auto trial = alns_solver.solve(
                *req, plan,
                post_cfg.fixedCostPerVehicle, post_cfg.costPerKm,
                budget_per_start,
                post_cfg.reloadMin, run_seed);
            if (out_stats && s == 0) *out_stats = alns_solver.stats();

            double trial_obj = planObj(trial);
            if (trial_obj < best_obj) {
                best_obj = trial_obj;
                best_plan = std::move(trial);
            }
        }
        plan = std::move(best_plan);
    } else {
        applyTwoOpt(plan, *req, post_cfg);
    }
    auto t2 = Clock::now();
    std::cout << "[Phase] " << (cfg.enableALNS ? "alns" : "2-opt")
              << "  obj=" << planObj(plan)
              << "  routes=" << plan.routes.size()
              << "  unassigned=" << plan.drops.size()
              << "  ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(t2 - t1).count() << "\n";

    hfvrptwb::orOptRelocate(plan, *req, post_cfg);
    auto t3 = Clock::now();
    std::cout << "[Phase] or-opt"
              << "  obj=" << planObj(plan)
              << "  routes=" << plan.routes.size()
              << "  unassigned=" << plan.drops.size()
              << "  ms=" << std::chrono::duration_cast<std::chrono::milliseconds>(t3 - t2).count() << "\n";

    writeResponse(*req, post_cfg, plan, resp);

    auto ms = [](auto a, auto b) {
        return std::chrono::duration_cast<std::chrono::milliseconds>(b - a).count();
    };
    std::cout << "[Solve] construction=" << ms(t0, t1)
              << "ms  " << (cfg.enableALNS ? "alns" : "2-opt") << "=" << ms(t1, t2)
              << "ms  or-opt=" << ms(t2, t3)
              << "ms  total=" << ms(t0, t3)
              << "ms  routes=" << resp->routes_size()
              << "  unassigned=" << resp->unassigned_size()
              << "  objective=" << resp->objective() << "\n";

    return grpc::Status::OK;
}

} // namespace

grpc::Status SolverServiceImpl::Solve(grpc::ServerContext*,
                                      const solver::SolveRequest* req,
                                      solver::SolveResponse* resp)
{
    return solveRequest(req, cfg_, resp);
}

solver::SolveResponse SolverV2::Solve(const solver::SolveRequest& req) {
    solver::SolveResponse resp;
    solveRequest(&req, cfg_, &resp, &last_alns_stats_);
    return resp;
}
