# Trimmed from the lite-exchange root CMakeLists.txt: same standard and flags,
# no liburing/benchmark deps (the gateway and benches aren't part of this pad).
cmake_minimum_required(VERSION 3.20)
project(lx-interview LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Debug CACHE STRING "Build type" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic -Werror)
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -g)
  add_link_options(-fsanitize=address,undefined)
endif()

find_package(Threads REQUIRED)
find_package(GTest QUIET)

include_directories(include)

file(GLOB_RECURSE TEST_SOURCES CONFIGURE_DEPENDS test/*.cc)
add_executable(tests ${TEST_SOURCES})
target_link_libraries(tests PRIVATE Threads::Threads)

if(GTest_FOUND)
  target_link_libraries(tests PRIVATE GTest::gtest_main)
else()
  message(STATUS "GoogleTest not found: using third_party/mini_gtest")
  target_sources(tests PRIVATE third_party/mini_gtest/gtest_main.cc)
  target_include_directories(tests SYSTEM PRIVATE third_party/mini_gtest)
endif()
