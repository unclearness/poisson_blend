cmake_minimum_required (VERSION 3.8)
project (poisson_blend)

set (CMAKE_CXX_STANDARD 17)

include_directories(include/ app/ third_party/eigen/ third_party/lodepng/)

add_executable(poisson_blend include/nanopb/nanopb.h app/main.cpp third_party/lodepng/lodepng.cpp)

set_target_properties(poisson_blend PROPERTIES VS_DEBUGGER_WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}")