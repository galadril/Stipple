cmake_minimum_required(VERSION 3.21)

project(notrix
    VERSION 0.1.0
    DESCRIPTION "Open pixel firmware for the Ulanzi TC002"
    LANGUAGES CXX)

# NOTRIX core is portable C++17 with no external dependencies. It must compile
# identically for the host test runner, the Emscripten emulator and (later) the
# arm-linux-gnueabihf device target. See docs/adr/0003-framebuffer-renderer.md.
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Debug CACHE STRING "" FORCE)
endif()

option(NOTRIX_BUILD_TESTS    "Build the host unit and golden-image tests" ON)
option(NOTRIX_BUILD_EMULATOR "Build the Emscripten browser emulator"      OFF)
option(NOTRIX_WARNINGS_AS_ERRORS "Treat compiler warnings as errors"      OFF)

# A device build carries only what the device needs. The PNG encoder and the
# preview tool are host conveniences, and CLAUDE.md is explicit that imageio must
# never be linked into the memory-constrained device build - so make that a
# build-system fact rather than a habit.
option(NOTRIX_DEVICE_BUILD   "Cross build for the TC002: core only, no host tools" OFF)

# Included after the options above: NotrixWarnings reads them at include time.
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(NotrixWarnings)

if(EMSCRIPTEN)
    # The emulator target owns its own link flags; tests cannot run under WASM.
    set(NOTRIX_BUILD_TESTS OFF)
    set(NOTRIX_BUILD_EMULATOR ON)
endif()

add_subdirectory(firmware)

if(NOTRIX_BUILD_EMULATOR)
    add_subdirectory(simulator/web)
endif()

if(NOTRIX_BUILD_TESTS)
    enable_testing()
    add_subdirectory(firmware/tests)
endif()

message(STATUS "NOTRIX ${PROJECT_VERSION}")
message(STATUS "  device build : ${NOTRIX_DEVICE_BUILD}")
message(STATUS "  tests        : ${NOTRIX_BUILD_TESTS}")
message(STATUS "  emulator     : ${NOTRIX_BUILD_EMULATOR}")
message(STATUS "  warn-as-error: ${NOTRIX_WARNINGS_AS_ERRORS}")
