cmake_minimum_required(VERSION 3.20)
set(CMAKE_CXX_STANDARD 23)

project(harpoon LANGUAGES CXX)

add_executable(fileparse examples/fileparse.cc)
target_compile_features(fileparse PRIVATE cxx_std_23)
target_compile_options(fileparse PRIVATE 
    -mavx2
    -O3
    -march=native
    -mtune=native
    -DNDEBUG
)
target_include_directories(fileparse PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})




# Interview tests
add_executable(tests tests/tests.cc)
target_compile_features(tests PRIVATE cxx_std_23)
target_compile_options(tests PRIVATE -mavx2 -O2)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
