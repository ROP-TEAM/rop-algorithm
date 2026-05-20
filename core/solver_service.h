#pragma once
#include <grpcpp/grpcpp.h>
#include <random>
#include "alns/alns.h"
#include "solver.grpc.pb.h"

struct SolveConfig {
    double   fixedCostPerVehicle = 550.0;
    double   costPerKm           = 4.0003;
    uint32_t seed                = std::random_device{}();
    bool     enableALNS          = false;
    bool     enableMultiTrip     = false;
    bool     enableSectorRemoval = true;
    int      reloadMin           = 30;
    int      multiStartCount     = 2;
};

class SolverServiceImpl final : public solver::SolverService::Service {
public:
    explicit SolverServiceImpl(SolveConfig cfg = {}) : cfg_(cfg) {}

    grpc::Status Solve(grpc::ServerContext* ctx,
                       const solver::SolveRequest* req,
                       solver::SolveResponse* resp) override;

private:
    SolveConfig cfg_;
};

class SolverV2 {
public:
    explicit SolverV2(SolveConfig cfg = {}) : cfg_(cfg) {}
    solver::SolveResponse Solve(const solver::SolveRequest& req);

    const hfvrptwb::alns::OperatorStats& lastAlnsStats() const { return last_alns_stats_; }

private:
    SolveConfig                             cfg_;
    hfvrptwb::alns::OperatorStats           last_alns_stats_;
};
