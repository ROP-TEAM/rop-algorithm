cmake_minimum_required(VERSION 3.15)
project(ROPSolver)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf REQUIRED)

# Generate C++ code from solver.proto
get_filename_component(PROTO_FILE "${CMAKE_CURRENT_SOURCE_DIR}/../solver/proto/solver.proto" ABSOLUTE)
get_filename_component(PROTO_DIR  "${PROTO_FILE}" DIRECTORY)

set(PROTO_SRCS "${CMAKE_CURRENT_BINARY_DIR}/solver.pb.cc")
set(PROTO_HDRS "${CMAKE_CURRENT_BINARY_DIR}/solver.pb.h")
set(GRPC_SRCS  "${CMAKE_CURRENT_BINARY_DIR}/solver.grpc.pb.cc")
set(GRPC_HDRS  "${CMAKE_CURRENT_BINARY_DIR}/solver.grpc.pb.h")

add_custom_command(
    OUTPUT ${PROTO_SRCS} ${PROTO_HDRS} ${GRPC_SRCS} ${GRPC_HDRS}
    COMMAND protobuf::protoc
        --cpp_out=${CMAKE_CURRENT_BINARY_DIR}
        --grpc_out=${CMAKE_CURRENT_BINARY_DIR}
        --plugin=protoc-gen-grpc=$<TARGET_FILE:gRPC::grpc_cpp_plugin>
        -I${PROTO_DIR}
        ${PROTO_FILE}
    DEPENDS ${PROTO_FILE}
)

set(FEASIBILITY_SRCS
    feasibility/capacity.cpp
    feasibility/tags.cpp
    feasibility/max_distance.cpp
    feasibility/precedence.cpp
    feasibility/shift.cpp
    feasibility/break.cpp
    validator/route_validator.cpp
)

# gRPC solver binary — started by Go's solver/process package
add_executable(solver
    main.cpp
    solver_service.cpp
    priority_shape_clustering/slender_utils.cpp
    priority_shape_clustering/slender_solver.cpp
    ${FEASIBILITY_SRCS}
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_link_libraries(solver PRIVATE gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(solver PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Solve pipeline demo — requires gRPC/protobuf (uses SolveRequest/SolveResponse proto)
add_executable(demo_solve
    demo_solve.cpp
    solver_service.cpp
    priority_shape_clustering/slender_utils.cpp
    priority_shape_clustering/slender_solver.cpp
    ${FEASIBILITY_SRCS}
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_link_libraries(demo_solve PRIVATE gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(demo_solve PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# Benchmark harness — plug in new *_bench.cpp that includes bench_gen.h to extend
add_executable(bench_solve
    bench_solve.cpp
    bench_gen.cpp
    solver_service.cpp
    priority_shape_clustering/slender_utils.cpp
    priority_shape_clustering/slender_solver.cpp
    ${FEASIBILITY_SRCS}
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_link_libraries(bench_solve PRIVATE gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(bench_solve PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)

# V1 vs V2 comparison harness — Dobj%=0 on identical algo confirms harness is correct
add_executable(bench_compare
    bench_compare.cpp
    bench_gen.cpp
    solver_service.cpp
    priority_shape_clustering/slender_utils.cpp
    priority_shape_clustering/slender_solver.cpp
    ${FEASIBILITY_SRCS}
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_link_libraries(bench_compare PRIVATE gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(bench_compare PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
