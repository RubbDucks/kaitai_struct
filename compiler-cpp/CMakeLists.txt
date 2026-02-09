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

add_library(kscpp_codegen STATIC
  src/codegen.cpp
)
target_include_directories(kscpp_codegen PUBLIC src)
target_compile_features(kscpp_codegen PUBLIC cxx_std_17)

add_library(kscpp_frontend STATIC
  src/frontend.cpp
)
target_include_directories(kscpp_frontend PUBLIC src)
target_compile_features(kscpp_frontend PUBLIC cxx_std_17)

add_executable(kscpp
  src/main.cpp
)
target_link_libraries(kscpp PRIVATE kscpp_cli kscpp_ir kscpp_codegen kscpp_frontend)
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


add_executable(kscpp_codegen_tests
  tests/codegen_test.cpp
)
target_link_libraries(kscpp_codegen_tests PRIVATE kscpp_codegen kscpp_cli kscpp_ir)

add_test(NAME kscpp_cli_unit_tests COMMAND kscpp_cli_tests)
add_test(NAME kscpp_ir_unit_tests COMMAND kscpp_ir_tests)
add_test(NAME kscpp_codegen_unit_tests COMMAND kscpp_codegen_tests)

add_test(NAME kscpp_version_experimental COMMAND kscpp --version)
set_tests_properties(kscpp_version_experimental PROPERTIES
  PASS_REGULAR_EXPRESSION "experimental"
)

add_test(NAME kscpp_help COMMAND kscpp --help)
set_tests_properties(kscpp_help PROPERTIES
  PASS_REGULAR_EXPRESSION "Usage: kaitai-struct-compiler"
)


add_test(NAME kscpp_from_ir_valid_sample
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/valid_sample.ksir
)
set_tests_properties(kscpp_from_ir_valid_sample PROPERTIES
  PASS_REGULAR_EXPRESSION "IR validation succeeded"
)

add_test(NAME kscpp_from_ir_invalid_unknown_type
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/invalid_unknown_type.ksir
)
set_tests_properties(kscpp_from_ir_invalid_unknown_type PROPERTIES
  WILL_FAIL TRUE
)

add_test(NAME kscpp_from_ir_invalid_cycle
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/invalid_cycle.ksir
)
set_tests_properties(kscpp_from_ir_invalid_cycle PROPERTIES
  WILL_FAIL TRUE
)


add_test(NAME kscpp_from_ir_codegen_hello_world
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/hello_world_minimal.ksir -t cpp_stl --cpp-standard 17 -d ${CMAKE_CURRENT_BINARY_DIR}/generated_hello_world
)
set_tests_properties(kscpp_from_ir_codegen_hello_world PROPERTIES
  PASS_REGULAR_EXPRESSION "IR codegen succeeded"
)

add_test(NAME kscpp_from_ir_codegen_expr_subset_a
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/expr_subset_a.ksir -t cpp_stl --cpp-standard 17 -d ${CMAKE_CURRENT_BINARY_DIR}/generated_expr_subset_a
)
set_tests_properties(kscpp_from_ir_codegen_expr_subset_a PROPERTIES
  PASS_REGULAR_EXPRESSION "IR codegen succeeded"
)

add_test(NAME kscpp_from_ir_codegen_invalid_validation_target
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/invalid_validation_target.ksir -t cpp_stl --cpp-standard 17
)
set_tests_properties(kscpp_from_ir_codegen_invalid_validation_target PROPERTIES
  WILL_FAIL TRUE
)

add_test(NAME kscpp_from_ir_codegen_advanced_semantics_subset
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/advanced_semantics_subset.ksir -t cpp_stl --cpp-standard 17 -d ${CMAKE_CURRENT_BINARY_DIR}/generated_advanced_semantics_subset
)
set_tests_properties(kscpp_from_ir_codegen_advanced_semantics_subset PROPERTIES
  PASS_REGULAR_EXPRESSION "IR codegen succeeded"
)

add_test(NAME kscpp_from_ir_codegen_type_subset
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/type_subset.ksir -t cpp_stl --cpp-standard 17 -d ${CMAKE_CURRENT_BINARY_DIR}/generated_type_subset
)
set_tests_properties(kscpp_from_ir_codegen_type_subset PROPERTIES
  PASS_REGULAR_EXPRESSION "IR codegen succeeded"
)


add_test(NAME kscpp_from_ir_codegen_control_flow_subset
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/control_flow_subset.ksir -t cpp_stl --cpp-standard 17 -d ${CMAKE_CURRENT_BINARY_DIR}/generated_control_flow_subset
)
set_tests_properties(kscpp_from_ir_codegen_control_flow_subset PROPERTIES
  PASS_REGULAR_EXPRESSION "IR codegen succeeded"
)

add_test(NAME kscpp_from_ir_invalid_switch_duplicate_else
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/invalid_switch_duplicate_else.ksir
)
set_tests_properties(kscpp_from_ir_invalid_switch_duplicate_else PROPERTIES
  WILL_FAIL TRUE
)

add_test(NAME kscpp_from_ir_codegen_unsupported_dynamic_switch
  COMMAND kscpp --from-ir ${CMAKE_CURRENT_SOURCE_DIR}/tests/data/unsupported_dynamic_switch.ksir -t cpp_stl --cpp-standard 17
)
set_tests_properties(kscpp_from_ir_codegen_unsupported_dynamic_switch PROPERTIES
  WILL_FAIL TRUE
)
