# Standalone AP3 manifest-emit opt plugin (libap3pass.so).
#
# Deliberately independent of the heavy Smaug plugin so it builds against a stock
# LLVM-18 with modest resources:
#   cmake -S passes/ap3 -B passes/ap3/build -DLLVM_18_PREFIX=$(brew --prefix llvm@18)
#   cmake --build passes/ap3/build
#
# The same RevealView.cpp is compiled into libpasses.so too (see ../CMakeLists.txt).
cmake_minimum_required(VERSION 3.16)

set(LLVM_18_PREFIX "/usr/lib/llvm-18" CACHE PATH "Path to the LLVM 18 install")
if(NOT DEFINED CMAKE_CXX_COMPILER)
    set(CMAKE_CXX_COMPILER "${LLVM_18_PREFIX}/bin/clang++" CACHE FILEPATH "C++ compiler" FORCE)
endif()

project(ap3_pass LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE RelWithDebInfo)
endif()

# No version arg: LLVM's config version file rejects a bare "18" spec; the HINTS
# path pins us to the LLVM-18 install instead.
find_package(LLVM REQUIRED CONFIG HINTS "${LLVM_18_PREFIX}/lib/cmake/llvm")
message(STATUS "AP3 pass: LLVM ${LLVM_PACKAGE_VERSION} @ ${LLVM_DIR}")
add_definitions(${LLVM_DEFINITIONS})
include_directories(${LLVM_INCLUDE_DIRS})

# The LLVM-free manifest core (SHA-256 + canonical JSON + model + metadata).
add_subdirectory(${CMAKE_CURRENT_SOURCE_DIR}/../manifest ${CMAKE_CURRENT_BINARY_DIR}/manifest)

add_library(ap3pass MODULE
    RevealView.cpp
    ap3_plugin.cpp
)
target_include_directories(ap3pass PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    ${CMAKE_CURRENT_SOURCE_DIR}/../manifest/include
)
# The manifest core uses C++ exceptions (JSON parse errors); RevealView catches
# them. LLVM is built without RTTI, but our exception handling needs it, so keep
# RTTI on for this plugin's own TUs.
target_compile_options(ap3pass PRIVATE -fexceptions -frtti)
target_link_libraries(ap3pass PRIVATE ap3manifest)

if(APPLE)
    # LLVM plugins resolve symbols against the opt process at load time.
    target_link_options(ap3pass PRIVATE "-Wl,-undefined,dynamic_lookup")
endif()
