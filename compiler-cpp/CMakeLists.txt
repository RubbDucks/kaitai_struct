cmake_minimum_required(VERSION 3.16)

project(kaitai_struct_compiler_cpp
  VERSION 0.0.0
  DESCRIPTION "Experimental C++17 compiler path scaffold"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_executable(kscpp
  src/main.cpp
)

target_compile_features(kscpp PRIVATE cxx_std_17)

enable_testing()
add_test(NAME kscpp_version_experimental COMMAND kscpp --version)
set_tests_properties(kscpp_version_experimental PROPERTIES
  PASS_REGULAR_EXPRESSION "experimental"
)
