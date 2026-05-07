#include "solver_service.h"

#include "construction/adaptive_constructor.h"
#include "drop/drop_logic.h"
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
        req, vehicle, trips, cfg.fixedCostPerVehicle, cfg.costPerKm);
    if (!validation.feasible) {
        addDrop(resp, vehicle.id(), validation.code, validation.detail);
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

grpc::Status solveRequest(const solver::SolveRequest* req,
                          const SolveConfig& cfg,
                          solver::SolveResponse* resp)
{
    auto t0 = std::chrono::high_resolution_clock::now();

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

    auto plan = hfvrptwb::adaptiveConstruct(
        *req, cfg.fixedCostPerVehicle, cfg.costPerKm, cfg.enableMultiTrip, cfg.reloadMin);
    writeResponse(*req, cfg, plan, resp);

    auto t1 = std::chrono::high_resolution_clock::now();
    std::cout << "[Solve] adaptive construction finished in "
              << std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count()
              << " ms, routes=" << resp->routes_size()
              << ", unassigned=" << resp->unassigned_size()
              << ", objective=" << resp->objective() << "\n";

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
    solveRequest(&req, cfg_, &resp);
    return resp;
}
