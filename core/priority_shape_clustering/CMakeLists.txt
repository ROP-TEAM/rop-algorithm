cmake_minimum_required(VERSION 3.10)
project(PriorityShapeClustering)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Standalone demo — no gRPC required
add_executable(demo
    demo.cpp
    slender_utils.cpp
    slender_solver.cpp
)
