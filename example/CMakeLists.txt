cmake_minimum_required(VERSION 3.22)

project(papper-example LANGUAGES CXX)

add_subdirectory(.. papper)

add_executable(papper-example main.cpp)
target_link_libraries(papper-example PRIVATE papper::papper)
target_compile_definitions(papper-example PRIVATE PAPPER_DEFINE_SHORT_NAMES PAPPER_DEFINE_VERY_SHORT_NAMES)
