cmake_minimum_required(VERSION 3.24)
project(nw_network_cpp VERSION 0.1.0 LANGUAGES CXX)
option(NW_NETWORK_BUILD_TESTS "Build C++ parity tests" ON)
find_package(Python3 3.10 REQUIRED COMPONENTS Interpreter)

set(NW_ROOT "\${CMAKE_CURRENT_SOURCE_DIR}/..")
set(NW_GEN "\${CMAKE_CURRENT_BINARY_DIR}/generated")
file(MAKE_DIRECTORY "\${NW_GEN}/nw_network")
file(GLOB_RECURSE NW_STATE_RS CONFIGURE_DEPENDS "\${NW_ROOT}/src/states/*.rs")
set(NW_SCHEMA "\${NW_ROOT}/crates/nw-network-types/codegen/network-schema.json")
set(NW_GEN_HPP "\${NW_GEN}/nw_network/generated_states.hpp")
set(NW_MSG_HPP "\${NW_GEN}/nw_network/generated_messages.hpp")
set(NW_GEN_CPP "\${NW_GEN}/network_schema_generated.cpp")
set(NW_MANIFEST "\${NW_GEN}/port_manifest.json")

add_custom_command(
  OUTPUT "\${NW_GEN_HPP}" "\${NW_MSG_HPP}" "\${NW_GEN_CPP}" "\${NW_MANIFEST}"
  COMMAND "\${Python3_EXECUTABLE}" "\${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_cpp.py"
          --root "\${NW_ROOT}" --out "\${NW_GEN}" --strict
  DEPENDS "\${CMAKE_CURRENT_SOURCE_DIR}/tools/generate_cpp.py" "\${NW_SCHEMA}" \${NW_STATE_RS}
  VERBATIM
)
add_custom_target(nw_network_codegen DEPENDS "\${NW_GEN_HPP}" "\${NW_MSG_HPP}" "\${NW_GEN_CPP}" "\${NW_MANIFEST}")

add_library(nw_network_cpp STATIC src/network.cpp "\${NW_GEN_CPP}")
add_library(nw::network ALIAS nw_network_cpp)
add_dependencies(nw_network_cpp nw_network_codegen)
target_compile_features(nw_network_cpp PUBLIC cxx_std_20)
target_include_directories(nw_network_cpp PUBLIC "\${CMAKE_CURRENT_SOURCE_DIR}/include" "\${NW_GEN}")
if(MSVC)
  target_compile_options(nw_network_cpp PRIVATE /W4 /permissive- /Zc:__cplusplus)
else()
  target_compile_options(nw_network_cpp PRIVATE -Wall -Wextra -Wpedantic)
endif()

if(NW_NETWORK_BUILD_TESTS)
  enable_testing()
  foreach(name IN ITEMS core warboard replicated_container generated_coverage)
    add_executable("nw_\${name}_test" "tests/\${name}_test.cpp")
    target_link_libraries("nw_\${name}_test" PRIVATE nw::network)
    target_compile_features("nw_\${name}_test" PRIVATE cxx_std_20)
    add_test(NAME "nw_\${name}_test" COMMAND "nw_\${name}_test")
  endforeach()
endif()
