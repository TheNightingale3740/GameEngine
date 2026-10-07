cmake_minimum_required(VERSION 3.24)

project(Ember
    VERSION 0.1.0
    DESCRIPTION "Ember - a production-grade 3D game engine"
    LANGUAGES C CXX)

# ----------------------------------------------------------------------------
# Global settings
# ----------------------------------------------------------------------------

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
set(CMAKE_POSITION_INDEPENDENT_CODE ON)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin")
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib")

option(EMBER_BUILD_EDITOR "Build the Ember editor executable" ON)
option(EMBER_BUILD_RUNTIME "Build the Ember game runtime executable" ON)
option(EMBER_BUILD_TESTS "Build the Ember unit test suite" ON)
option(EMBER_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")

include(EmberWarnings)

# ----------------------------------------------------------------------------
# Third-party dependencies (fetched, pinned)
# ----------------------------------------------------------------------------

include(EmberDependencies)

# ----------------------------------------------------------------------------
# Subdirectories
# ----------------------------------------------------------------------------

add_subdirectory(sources/EmberCore)
add_subdirectory(sources/EmberEditor)
add_subdirectory(sources/EmberRuntime)

if(EMBER_BUILD_TESTS)
    enable_testing()
    add_subdirectory(tests)
endif()
