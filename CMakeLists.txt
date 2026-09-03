cmake_minimum_required(VERSION 3.22)
project(papper)

set(CMAKE_C_COMPILER clang)
set(CMAKE_CXX_COMPILER clang++)

add_executable(${PROJECT_NAME})
target_sources(${PROJECT_NAME}
  PRIVATE
    src/main.cpp
)

target_compile_options(${PROJECT_NAME}
  PRIVATE
    # -Wall -Wextra -pedantic -std=c++23 -fsanitize=address -g
    -Wall -Wextra -pedantic -std=c++23
)
# target_link_options(${PROJECT_NAME}
#   PRIVATE
#     -fsanitize=address
# )
