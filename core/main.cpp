#include <grpcpp/grpcpp.h>
#include "solver_service.h"

int main(int argc, char** argv) {
    std::string addr = argc > 1 ? argv[1] : "0.0.0.0:50051";

    SolverServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    server->Wait();
    return 0;
}
