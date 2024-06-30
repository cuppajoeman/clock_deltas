cmake_minimum_required(VERSION 3.0)
project(clock_deltas)

# Set C++ standard
set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ENET: reliable UDP
# SET(ENET_STATIC ON CACHE BOOL "" FORCE)
add_subdirectory(external_libraries/enet)
include_directories(external_libraries/enet/include)

# Server executable
add_executable(server server.cpp common.cpp ring_buffer.cpp)
target_link_libraries(server enet)

# Client executable
add_executable(client client.cpp common.cpp ring_buffer.cpp)
target_link_libraries(client enet)
