cmake_minimum_required(VERSION 3.22)

project(papper LANGUAGES CXX)

find_package(Threads REQUIRED)

add_library(papper INTERFACE)
add_library(papper::papper ALIAS papper)

target_compile_features(papper INTERFACE cxx_std_20)

target_include_directories(papper
  INTERFACE
    $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
    $<INSTALL_INTERFACE:include>
)

target_link_libraries(papper
  INTERFACE
    Threads::Threads
)

include(CTest)
if(BUILD_TESTING)
  add_subdirectory(tests)
endif()

option(BUILD_BENCHMARK "Build benchmarks" ON)
if(BUILD_BENCHMARK)
  add_subdirectory(benchmark)
endif()
