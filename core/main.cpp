#include <grpcpp/grpcpp.h>
#include <cstdlib>
#include "solver_service.h"

int main(int argc, char** argv) {
    std::string addr = argc > 1 ? argv[1] : "0.0.0.0:50051";

    SolveConfig cfg;
    // SPEED_WEIGHT env var: 0.0 = minimize cost, 1.0 = minimize duration
    if (const char* sw = std::getenv("SPEED_WEIGHT")) {
        cfg.speedWeight = std::stod(sw);
    }

    SolverServiceImpl service(cfg);
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    server->Wait();
    return 0;
}
