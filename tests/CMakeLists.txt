cmake_minimum_required(VERSION 3.22)

project(papper-tests LANGUAGES CXX)

add_subdirectory(.. papper)

include(FetchContent)
FetchContent_Declare(
  doctest
  GIT_REPOSITORY https://github.com/doctest/doctest.git
  GIT_TAG v2.4.12
  GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(doctest)

# Optional sanitizer, e.g. -DPAPPER_SANITIZE=thread or -DPAPPER_SANITIZE=address
set(PAPPER_SANITIZE "" CACHE STRING "Sanitizer to build the tests with")
if(PAPPER_SANITIZE)
  add_compile_options(-fsanitize=${PAPPER_SANITIZE} -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=${PAPPER_SANITIZE})
endif()

add_library(papper-test-main STATIC test_main.cpp)
target_link_libraries(papper-test-main PUBLIC papper::papper doctest::doctest)
target_compile_options(papper-test-main PUBLIC -Wall -Wextra)

# Unit tests for the internal building blocks
add_executable(papper-unit-tests
  test_queue.cpp
  test_codec.cpp
  test_pattern.cpp
)
target_link_libraries(papper-unit-tests PRIVATE papper-test-main)

# End-to-end tests through the public API. Kept in their own executable since
# the backend is a process-wide singleton.
add_executable(papper-logging-tests test_logging.cpp)
target_link_libraries(papper-logging-tests PRIVATE papper-test-main)

enable_testing()
add_test(NAME unit COMMAND papper-unit-tests)
add_test(NAME logging COMMAND papper-logging-tests)
set_tests_properties(unit logging PROPERTIES TIMEOUT 120)
