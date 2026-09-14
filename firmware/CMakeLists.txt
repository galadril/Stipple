# NOTRIX core — portable, dependency-free, platform-agnostic.
#
# Everything in this target sits ABOVE the blueprint §53 boundary and must build
# unchanged for host tests, the WASM emulator and the ARM device. Nothing here
# may include a platform header or assume an OS facility.

add_library(notrix_core
    src/graphics/Canvas.cpp
    src/demo/TestPattern.cpp
    src/text/Font5x7.cpp
    src/text/Text.cpp
    src/input/InputMapper.cpp
    src/json/Json.cpp
    src/scene/Scene.cpp
)

target_include_directories(notrix_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(notrix_core PUBLIC notrix_warnings)

add_library(notrix::core ALIAS notrix_core)

# PNG encoding is a host/emulator convenience (golden-image failure snapshots,
# emulator screenshots). It is deliberately NOT part of notrix_core so it can
# never be linked into the memory-constrained device build by accident.
add_library(notrix_imageio
    imageio/src/Png.cpp
)

target_include_directories(notrix_imageio
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/imageio/include")

target_link_libraries(notrix_imageio PUBLIC notrix_core)

add_library(notrix::imageio ALIAS notrix_imageio)

# The simulator adapter — the non-device half of the §53 boundary.
#
# A separate target on purpose: notrix_core must never depend on an adapter, and
# keeping them apart is what makes that violation a link error rather than a
# code review comment. The TC002 adapter becomes its sibling in Phase 7.
add_library(notrix_platform_simulator
    src/platform/simulator/SimulatorPlatform.cpp
)

target_link_libraries(notrix_platform_simulator PUBLIC notrix_core)

add_library(notrix::platform_simulator ALIAS notrix_platform_simulator)

# Host-only frame previewer. Renders through the simulator adapter and writes
# PNGs, so output can be inspected without an Emscripten toolchain.
if(NOT EMSCRIPTEN)
    add_executable(notrix_preview tools/preview/main.cpp)
    target_link_libraries(notrix_preview PRIVATE
        notrix_core
        notrix_imageio
        notrix_platform_simulator
        notrix_warnings)
endif()
