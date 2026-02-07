cmake_minimum_required(VERSION 3.16)

project(kaitai_struct_compiler_cpp
  VERSION 0.0.0
  DESCRIPTION "Experimental C++17 compiler path scaffold"
  LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(kscpp_ir STATIC
  src/ir.cpp
)
target_include_directories(kscpp_ir PUBLIC src)
target_compile_features(kscpp_ir PUBLIC cxx_std_17)

add_library(kscpp_cli STATIC
  src/cli_options.cpp
)
target_include_directories(kscpp_cli PUBLIC src)
target_compile_features(kscpp_cli PUBLIC cxx_std_17)

add_executable(kscpp
  src/main.cpp
)
target_link_libraries(kscpp PRIVATE kscpp_cli)
target_compile_features(kscpp PRIVATE cxx_std_17)

enable_testing()

add_executable(kscpp_cli_tests
  tests/cli_options_test.cpp
)
target_link_libraries(kscpp_cli_tests PRIVATE kscpp_cli)

add_executable(kscpp_ir_tests
  tests/ir_roundtrip_test.cpp
)
target_link_libraries(kscpp_ir_tests PRIVATE kscpp_ir)

add_test(NAME kscpp_cli_unit_tests COMMAND kscpp_cli_tests)
add_test(NAME kscpp_ir_unit_tests COMMAND kscpp_ir_tests)

add_test(NAME kscpp_version_experimental COMMAND kscpp --version)
set_tests_properties(kscpp_version_experimental PROPERTIES
  PASS_REGULAR_EXPRESSION "experimental"
)

add_test(NAME kscpp_help COMMAND kscpp --help)
set_tests_properties(kscpp_help PROPERTIES
  PASS_REGULAR_EXPRESSION "Usage: kaitai-struct-compiler"
)
