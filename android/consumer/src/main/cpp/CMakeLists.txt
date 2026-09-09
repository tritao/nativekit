cmake_minimum_required(VERSION 3.22.1)
project(nativekit_consumer LANGUAGES CXX)

find_package(nativekit REQUIRED CONFIG)

add_library(nativekit_consumer SHARED runtime.cpp)
target_compile_features(nativekit_consumer PRIVATE cxx_std_17)
target_link_libraries(nativekit_consumer PRIVATE nativekit::nativekit)
