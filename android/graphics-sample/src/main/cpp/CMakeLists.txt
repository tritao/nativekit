cmake_minimum_required(VERSION 3.22.1)
project(nativekit_graphics_sample LANGUAGES CXX)

find_package(nativekit REQUIRED CONFIG)

add_library(nativekit_graphics_sample SHARED renderer.cpp)
target_compile_features(nativekit_graphics_sample PRIVATE cxx_std_17)
target_link_libraries(nativekit_graphics_sample PRIVATE nativekit::nativekit)
