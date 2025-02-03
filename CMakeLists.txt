cmake_minimum_required(VERSION 3.0)
project(clock_deltas)

set(CMAKE_CXX_STANDARD 11)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

file(GLOB_RECURSE SOURCES "src/*.cpp")
add_executable(${PROJECT_NAME} ${SOURCES})

find_package(enet)
target_link_libraries(${PROJECT_NAME} enet::enet)
