cmake_minimum_required(VERSION 3.22.1)
set(CMAKE_CXX_STANDARD 20)

project(cassia)

# Libraries
add_subdirectory(libs)

# Cassia JNI
add_library(cassia SHARED native-lib.cpp)

target_link_libraries(cassia android log)
