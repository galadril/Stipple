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
    src/core/Base64.cpp
    src/core/Checksum.cpp
    src/core/Log.cpp
    src/core/Rgb.cpp
    src/render/FrameScheduler.cpp
    src/render/Transition.cpp
    src/asset/IconStore.cpp
    src/apps/BatteryApp.cpp
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

# The TC002 adapter — the device half of the §53 boundary, and the sibling the
# comment above promised.
#
# Device-only because it is POSIX: it opens /dev/spidev0.0 and issues ioctls,
# which neither the host test runner nor a browser can do. notrix_core must
# never depend on it, and keeping them separate targets is what makes that
# violation a link error rather than a review comment.
if(NOTRIX_DEVICE_BUILD)
    add_library(notrix_platform_tc002
        src/platform/tc002/Tc002Display.cpp
        src/platform/tc002/Tc002HttpServer.cpp
        src/platform/tc002/Tc002Input.cpp
        src/platform/tc002/Tc002Mcu.cpp
        src/platform/tc002/Tc002MqttClient.cpp
        src/platform/tc002/Tc002Platform.cpp
    )

    target_link_libraries(notrix_platform_tc002 PUBLIC notrix_core notrix_warnings)

    add_library(notrix::platform_tc002 ALIAS notrix_platform_tc002)
endif()

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

# Asks the vendor HAL what it offers without calling into it. Dynamically
# linked on purpose - it must dlopen libzkhw.so, which a static binary cannot
# do - so it needs the bullseye toolchain, whose glibc predates the
# __libc_start_main re-versioning. See tooling/cross/Containerfile.bullseye.
if(NOTRIX_DEVICE_BUILD)
    add_executable(notrix_hal_probe tools/hal_probe/main.cpp)
    target_link_libraries(notrix_hal_probe PRIVATE ${CMAKE_DL_LIBS} notrix_warnings)
endif()

# The last unknown in the display protocol: channel order.
#
# The spidev capture settled the device node, the SPI mode, the 3072-byte frame
# and the 64-column stride, but every lit pixel in it was white - and white is
# identical under RGB, GRB and BGR. This writes three bands with deliberately
# unequal channels so a person can look at the panel and say which is which.
# It is the one question no amount of further capture can answer.
#
# Plain C, and the only C in the tree, so the language is enabled here rather
# than in the top-level project() - a host build has no reason to gain a C
# compiler requirement for a file it never compiles.
#
# Static for the same reason as the smoke test, and it costs nothing here: this
# talks to spidev through ioctl and write, and dlopens nothing.
#
# notrix_warnings is deliberately NOT linked. It carries -Wold-style-cast and
# friends, which GCC reports as "valid for C++ but not for C" - a warning that
# the device preset's warnings-as-errors would turn into a build failure.
# NOTRIX itself, on the device.
#
# Runs ApplicationHost - the same startup, loop, boot record and safe-mode
# fallback the emulator and the host tests run - over the TC002 platform
# adapter. This is the firmware; notrix_device_panel below is the bring-up demo
# that preceded it and stays because it isolates the display from everything
# else when something goes wrong.
if(NOTRIX_DEVICE_BUILD)
    add_executable(notrix_device tools/device/main.cpp)
    target_link_libraries(notrix_device PRIVATE
        notrix_core
        notrix_platform_tc002
        notrix_warnings)
    target_link_options(notrix_device PRIVATE -static)
endif()

# NOTRIX core rendering on the real panel.
#
# The first thing that puts *our* renderer on the device rather than bytes
# written by hand against the wire: Framebuffer, Canvas and demo::drawTestPattern
# are the same code the emulator and the golden tests run, with only the display
# adapter swapped. Static for the same reason as the smoke test.
if(NOTRIX_DEVICE_BUILD)
    add_executable(notrix_device_panel tools/device_panel/main.cpp)
    target_link_libraries(notrix_device_panel PRIVATE
        notrix_core
        notrix_platform_tc002
        notrix_warnings)
    target_link_options(notrix_device_panel PRIVATE -static)
endif()

if(NOTRIX_DEVICE_BUILD)
    enable_language(C)

    add_executable(notrix_panel_test tools/panel_test/main.c)
    target_compile_options(notrix_panel_test PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    target_link_options(notrix_panel_test PRIVATE -static)

    # Prints what the physical controls actually send. /proc says event67
    # declares four keys; the device tree may well name more than the case has.
    # Safe to run alongside zkgui - evdev delivers to every reader.
    add_executable(notrix_input_probe tools/input_probe/main.c)
    target_compile_options(notrix_input_probe PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    target_link_options(notrix_input_probe PRIVATE -static)

    # Reads the MCU link. The only source of battery state on this device -
    # there is no power_supply class, no hwmon and no IIO.
    add_executable(notrix_mcu_probe tools/mcu_probe/main.c)
    target_compile_options(notrix_mcu_probe PRIVATE
        -Wall -Wextra -Wpedantic -Wshadow -Wconversion -Wsign-conversion)
    target_link_options(notrix_mcu_probe PRIVATE -static)
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
