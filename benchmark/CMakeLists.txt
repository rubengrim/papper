cmake_minimum_required(VERSION 3.22)

if(POLICY CMP0169)
  cmake_policy(SET CMP0169 OLD)
endif()

include(FetchContent)

# Fetch fmt library
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG        11.1.3
)
FetchContent_MakeAvailable(fmt)

# Fetch fmtlog library
FetchContent_Declare(
  fmtlog_src
  GIT_REPOSITORY https://github.com/MengRao/fmtlog.git
  GIT_TAG        main
)
FetchContent_GetProperties(fmtlog_src)
if(NOT fmtlog_src_POPULATED)
  FetchContent_Populate(fmtlog_src)
endif()

# Define fmtlog library target
add_library(fmtlog STATIC
  ${fmtlog_src_SOURCE_DIR}/fmtlog.cc
)

target_include_directories(fmtlog PUBLIC
  ${fmtlog_src_SOURCE_DIR}
)

target_link_libraries(fmtlog PUBLIC
  fmt::fmt
  Threads::Threads
)

target_compile_features(fmtlog PUBLIC cxx_std_20)

# Sizing fmtlog queue to 64MB to avoid drops during latency benchmarks
target_compile_definitions(fmtlog PUBLIC
  FMTLOG_QUEUE_SIZE=67108864
)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(fmtlog PRIVATE -O3 -DNDEBUG)
endif()

add_executable(benchmark_frontend
  benchmark_common.h
  papper_bench.h
  papper_bench.cpp
  fmtlog_bench.h
  fmtlog_bench.cpp
  main.cpp
)

target_link_libraries(benchmark_frontend
  PRIVATE
    papper::papper
    fmtlog
    Threads::Threads
)

target_compile_features(benchmark_frontend PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  target_compile_options(benchmark_frontend PRIVATE -O3 -DNDEBUG -Wno-interference-size)
endif()
