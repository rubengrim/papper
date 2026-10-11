cmake_minimum_required(VERSION 3.22)

project(papper-example LANGUAGES CXX)

# Must be set before add_subdirectory, otherwise papper's dependencies (fmt)
# are built without optimizations
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_subdirectory(.. papper)
set(CMAKE_EXPORT_COMPILE_COMMANDS TRUE)

add_executable(papper-example main.cpp)
target_link_libraries(papper-example PRIVATE papper::papper)
target_compile_definitions(papper-example PRIVATE PAPPER_DEFINE_SHORT_NAMES PAPPER_DEFINE_VERY_SHORT_NAMES)
