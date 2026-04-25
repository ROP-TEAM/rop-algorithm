cmake_minimum_required(VERSION 3.10)

project(PriorityShapeClustering)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(solver 
    main.cpp 
    slender_utils.cpp 
    slender_solver.cpp 
    slender_utils.h 
    slender_solver.h 
    types.h
)
