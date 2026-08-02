# AP3 manifest core - deliberately LLVM-free so it builds and tests anywhere:
# natively for a fast loop, and inside the hermetic image. The passes plugin
# links this static lib; the RevealView LLVM pass is a thin adapter over it.
#
# Standalone build (native, no LLVM):
#   cmake -S passes/manifest -B passes/manifest/build && cmake --build passes/manifest/build
#   ctest --test-dir passes/manifest/build --output-on-failure
cmake_minimum_required(VERSION 3.16)
project(ap3_manifest_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)  # linked into the shared plugin
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

add_library(ap3manifest STATIC
    src/Sha256.cpp
    src/CanonicalJson.cpp
    src/Manifest.cpp
    src/Ap3Metadata.cpp
)
target_include_directories(ap3manifest PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

# Tests are only wired up for a standalone (top-level) configure, so pulling the
# lib into the LLVM plugin build does not require enable_testing there.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
    enable_testing()

    add_executable(test_sha256 tests/test_sha256.cpp)
    target_link_libraries(test_sha256 PRIVATE ap3manifest)
    target_include_directories(test_sha256 PRIVATE tests)
    add_test(NAME sha256 COMMAND test_sha256)

    add_executable(test_conformance tests/test_conformance.cpp)
    target_link_libraries(test_conformance PRIVATE ap3manifest)
    target_include_directories(test_conformance PRIVATE tests)
    add_test(NAME conformance
             COMMAND test_conformance ${CMAKE_CURRENT_SOURCE_DIR}/tests/golden_vectors.json)

    add_executable(test_ap3_metadata tests/test_ap3_metadata.cpp)
    target_link_libraries(test_ap3_metadata PRIVATE ap3manifest)
    target_include_directories(test_ap3_metadata PRIVATE tests)
    add_test(NAME ap3_metadata
             COMMAND test_ap3_metadata
                     ${CMAKE_CURRENT_SOURCE_DIR}/../../benchmarks/metadata/psi.ll.json)
endif()
