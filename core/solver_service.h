#pragma once
#include <grpcpp/grpcpp.h>
#include "solver.grpc.pb.h"

class SolverServiceImpl final : public solver::SolverService::Service {
public:
    grpc::Status Solve(grpc::ServerContext* ctx,
                       const solver::SolveRequest* req,
                       solver::SolveResponse* resp) override;
};
