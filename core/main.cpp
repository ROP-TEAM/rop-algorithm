#include <grpcpp/grpcpp.h>
#include "priority_shape_clustering/solver_service.h"

int main(int argc, char** argv) {
    std::string port = argc > 1 ? argv[1] : "50051";
    std::string addr = "0.0.0.0:" + port;

    SolverServiceImpl service;
    grpc::ServerBuilder builder;
    builder.AddListeningPort(addr, grpc::InsecureServerCredentials());
    builder.RegisterService(&service);
    auto server = builder.BuildAndStart();
    server->Wait();
    return 0;
}
