cmake_minimum_required(VERSION 3.22.1)
set(CMAKE_CXX_STANDARD 20)

project(cassia)

# Libraries
add_subdirectory(libs)

# Cassia
file(GLOB_RECURSE cassia_SRC CONFIGURE_DEPENDS
        "cassia/*.cpp" "cassia/*.c"
)

add_library(cassia SHARED native_lib.cpp ${cassia_SRC})

target_link_libraries(cassia android log fmt::fmt)
