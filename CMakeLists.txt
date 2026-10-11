cmake_minimum_required(VERSION 3.22)

project(papper LANGUAGES CXX)

find_package(Threads REQUIRED)

include(FetchContent)
FetchContent_Declare(
  fmt
  GIT_REPOSITORY https://github.com/fmtlib/fmt.git
  GIT_TAG 12.1.0
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(fmt)

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
    fmt::fmt
)
