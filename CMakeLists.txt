# cmake minimum version required
cmake_minimum_required(VERSION 3.16)


set(CMAKE_C_COMPILER "/usr/bin/gcc")
set(CMAKE_C_FLAGS "-Wall")
set(CMAKE_GENERATOR "Unix Makefiles")
add_definitions(-D_GNU_SOURCE)

project(fuse-extent VERSION 1.0.0)

configure_file(cmakeConfig.h.in ${CMAKE_CURRENT_SOURCE_DIR}/include/cmakeConfig.h)

include_directories(include)


add_subdirectory(lib)
add_subdirectory(example)
enable_testing()
add_subdirectory(test)
