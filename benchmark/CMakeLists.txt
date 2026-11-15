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
# fmtlog's own CMakeLists builds its bundled fmt and its tests with global
# flags, so only fetch the sources (no submodules, SOURCE_SUBDIR without a
# CMakeLists.txt) and use it header-only with the same fmt as papper
FetchContent_Declare(
  fmtlog
  GIT_REPOSITORY https://github.com/MengRao/fmtlog.git
  GIT_TAG v2.3.0
  GIT_SHALLOW TRUE
  GIT_SUBMODULES ""
  SOURCE_SUBDIR no-cmake
)
FetchContent_MakeAvailable(quill spdlog fmtlog)

find_package(Threads REQUIRED)
add_library(fmtlog INTERFACE)
target_include_directories(fmtlog SYSTEM INTERFACE ${fmtlog_SOURCE_DIR})
target_link_libraries(fmtlog INTERFACE fmt::fmt Threads::Threads)
target_compile_definitions(fmtlog
  INTERFACE
    FMTLOG_HEADER_ONLY
    # Same queue size as papper uses in latency mode. fmtlog's queues can't
    # grow, so block instead of dropping messages when one is full, like
    # spdlog. Latency mode never fills the queue so it is unaffected
    FMTLOG_QUEUE_SIZE=524288
    FMTLOG_BLOCK=1
)

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
add_benchmark(bench_fmtlog fmtlog)
