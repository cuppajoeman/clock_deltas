cmake_minimum_required(VERSION 3.0)
project(clock_deltas)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ENET: reliable UDP
# SET(ENET_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(external_libraries/enet)
include_directories(external_libraries/enet/include)

add_executable(clock_deltas clock_deltas.cpp)
target_link_libraries(clock_deltas enet)

