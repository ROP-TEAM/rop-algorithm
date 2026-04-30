cmake_minimum_required(VERSION 3.15)
project(ROPSolver)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(gRPC CONFIG REQUIRED)
find_package(Protobuf CONFIG REQUIRED)

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

# gRPC solver binary — started by Go's solver/process package
add_executable(solver
    main.cpp
    priority_shape_clustering/solver_service.cpp
    priority_shape_clustering/slender_utils.cpp
    priority_shape_clustering/slender_solver.cpp
    ${PROTO_SRCS}
    ${GRPC_SRCS}
)
target_link_libraries(solver PRIVATE gRPC::grpc++ protobuf::libprotobuf)
target_include_directories(solver PRIVATE
    ${CMAKE_CURRENT_BINARY_DIR}
    ${CMAKE_CURRENT_SOURCE_DIR}
)
