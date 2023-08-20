cmake_minimum_required(VERSION 3.10.2)
project(cassiasrv LANGUAGES C CXX ASM VERSION 1.0)
set(CMAKE_CXX_STANDARD 20)

add_library(cassiasrv SHARED vk_helpers.cpp nekomposite.cpp server.cpp)
target_link_libraries(cassiasrv PUBLIC Vulkan::Headers log android)

# Vulkan + Vulkan-Hpp
#target_include_directories(cassiasrv SYSTEM PUBLIC "../libs/Vulkan-Headers/include")
target_compile_definitions(cassiasrv PRIVATE VULKAN_HPP_NO_STRUCT_CONSTRUCTORS VK_USE_PLATFORM_ANDROID_KHR VULKAN_HPP_NO_SETTERS VULKAN_HPP_NO_SMART_HANDLE VULKAN_HPP_DISPATCH_LOADER_DYNAMIC=1 VULKAN_HPP_ENABLE_DYNAMIC_LOADER_TOOL=0)
