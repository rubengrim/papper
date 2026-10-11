cmake_minimum_required(VERSION 3.22)

project(papper-benchmark LANGUAGES CXX)

# Benchmarks are meaningless without optimizations
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(.. papper)

# The libraries papper is compared against
include(FetchContent)
FetchContent_Declare(
  quill
  GIT_REPOSITORY https://github.com/odygrd/quill.git
  GIT_TAG v13.0.0
  GIT_SHALLOW TRUE
)
FetchContent_Declare(
  spdlog
  GIT_REPOSITORY https://github.com/gabime/spdlog.git
  GIT_TAG v1.17.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(quill spdlog)

# One executable per library, since each library has its own process-wide
# backend
function(add_benchmark name library)
  add_executable(${name} ${name}.cpp)
  target_link_libraries(${name} PRIVATE ${library})
  target_compile_features(${name} PRIVATE cxx_std_20)
  target_compile_options(${name} PRIVATE -Wall -Wextra)
endfunction()

add_benchmark(bench_papper papper::papper)
add_benchmark(bench_quill quill::quill)
add_benchmark(bench_spdlog spdlog::spdlog)
