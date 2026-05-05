#pragma once
#include <grpcpp/grpcpp.h>
#include "solver.grpc.pb.h"

struct SolveConfig {
    double   fixedCostPerVehicle = 450.0;
    double   costPerKm           = 40.02;
    int      randomTrials        = 30;
    uint32_t seed                = 42;
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

private:
    SolveConfig cfg_;
};
