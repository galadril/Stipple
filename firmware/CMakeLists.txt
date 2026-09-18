# NOTRIX core — portable, dependency-free, platform-agnostic.
#
# Everything in this target sits ABOVE the blueprint §53 boundary and must build
# unchanged for host tests, the WASM emulator and the ARM device. Nothing here
# may include a platform header or assume an OS facility.

# The configuration UI is compiled into the binary rather than read from disk.
# A device whose storage has gone wrong is exactly when someone needs its config
# page, so the page must not depend on that storage.
include("${CMAKE_SOURCE_DIR}/cmake/EmbedWebAssets.cmake")

set(NOTRIX_WEB_GENERATED "${CMAKE_CURRENT_BINARY_DIR}/generated/WebAssets.generated.cpp")
notrix_embed_web_assets(
    "${CMAKE_CURRENT_SOURCE_DIR}/web"
    "${NOTRIX_WEB_GENERATED}"
    NOTRIX_WEB_SOURCES)

# Re-run configure when a UI source changes, so editing a page is enough to
# rebuild it — without this the generated file would silently go stale.
set_property(DIRECTORY APPEND
    PROPERTY CMAKE_CONFIGURE_DEPENDS ${NOTRIX_WEB_SOURCES})

add_library(notrix_core
    ${NOTRIX_WEB_GENERATED}
    src/web/StaticFiles.cpp
    src/graphics/Canvas.cpp
    src/demo/TestPattern.cpp
    src/text/Font5x7.cpp
    src/text/Text.cpp
    src/text/Scroll.cpp
    src/notify/Notifications.cpp
    src/input/InputMapper.cpp
    src/json/Json.cpp
    src/scene/Scene.cpp
    src/core/Checksum.cpp
    src/core/Log.cpp
    src/core/Rgb.cpp
    src/render/FrameScheduler.cpp
    src/asset/IconStore.cpp
    src/apps/ClockApp.cpp
    src/apps/SplashScreen.cpp
    src/host/ApplicationHost.cpp
    src/app/AppRegistry.cpp
    src/app/Carousel.cpp
    src/config/Config.cpp
    src/api/Http.cpp
    src/api/JsonWriter.cpp
    src/api/ApiServer.cpp
    src/mqtt/MqttBridge.cpp
    src/mqtt/MqttService.cpp
)

target_include_directories(notrix_core
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/include")

target_link_libraries(notrix_core PUBLIC notrix_warnings)

add_library(notrix::core ALIAS notrix_core)

# PNG encoding is a host/emulator convenience (golden-image failure snapshots,
# emulator screenshots). It is deliberately NOT part of notrix_core so it can
# never be linked into the memory-constrained device build by accident - and on
# a device build it is not compiled at all, so "by accident" needs a deliberate
# edit here first.
if(NOT NOTRIX_DEVICE_BUILD)
add_library(notrix_imageio
    imageio/src/Png.cpp
)

target_include_directories(notrix_imageio
    PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}/imageio/include")

target_link_libraries(notrix_imageio PUBLIC notrix_core)

add_library(notrix::imageio ALIAS notrix_imageio)
endif()

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

# Smoke test for the target architecture. Small enough to push over ADB, and it
# answers the questions that must be settled before anything is flashed: does the
# core run on this CPU at all, and what does the binary actually depend on.
if(NOTRIX_DEVICE_BUILD)
    add_executable(notrix_device_smoke tools/device_smoke/main.cpp)
    target_link_libraries(notrix_device_smoke PRIVATE
        notrix_core
        notrix_platform_simulator
        notrix_warnings)

    # Statically linked, deliberately.
    #
    # A dynamically linked Debian 12 binary requires GLIBC_2.34 purely for
    # __libc_start_main, because that symbol was re-versioned when glibc merged
    # libpthread into libc. The TC002 is an embedded SigmaStar device whose glibc
    # is a §46 unknown and is very unlikely to be that new, so a dynamic binary
    # would fail to start with an error naming a symbol rather than the real
    # cause.
    #
    # Static linking removes the entire question: no interpreter, no versioned
    # symbols, nothing to be too old. It costs binary size, which the 8 MiB res
    # partition can afford far more easily than it can afford a bring-up spent
    # bisecting toolchains.
    #
    # The one thing this gives up is NSS - getaddrinfo cannot dlopen its modules
    # in a static binary. Nothing in core resolves a hostname; when the MQTT
    # transport arrives in Phase 7 and does, that is the moment to revisit this,
    # and the probe will have reported the real glibc version by then.
    target_link_options(notrix_device_smoke PRIVATE -static)
endif()

# Host-only frame previewer. Renders through the simulator adapter and writes
# PNGs, so output can be inspected without an Emscripten toolchain.
if(NOT EMSCRIPTEN AND NOT NOTRIX_DEVICE_BUILD)
    add_executable(notrix_preview tools/preview/main.cpp)
    target_link_libraries(notrix_preview PRIVATE
        notrix_core
        notrix_imageio
        notrix_platform_simulator
        notrix_warnings)
endif()
