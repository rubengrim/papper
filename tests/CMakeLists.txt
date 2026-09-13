find_package(Catch2 3 QUIET)

if(NOT Catch2_FOUND)
  include(FetchContent)
  FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG        v3.7.1
  )
  FetchContent_MakeAvailable(Catch2)
endif()

add_executable(papper_tests
  test_codec.cpp
  test_queue.cpp
  test_pattern.cpp
  test_sink.cpp
  test_papper.cpp
)

target_link_libraries(papper_tests
  PRIVATE
    papper::papper
    Catch2::Catch2WithMain
)

target_compile_features(papper_tests PRIVATE cxx_std_20)

if(CMAKE_CXX_COMPILER_ID STREQUAL "GNU")
  target_compile_options(papper_tests PRIVATE -Wno-interference-size)
endif()

include(Catch)
catch_discover_tests(papper_tests)
