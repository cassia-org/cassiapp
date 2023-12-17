cmake_minimum_required(VERSION 3.22.1)
set(CMAKE_CXX_STANDARD 20)

# Vulkan Headers
add_subdirectory(Vulkan-Headers)

# CPM
include(CPM.cmake)

CPMAddPackage("gh:fmtlib/fmt#10.1.1")
