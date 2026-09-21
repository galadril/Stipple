add_executable(notrix_tests
    support/TestFramework.cpp
    support/Golden.cpp
    test_geometry.cpp
    test_rgb.cpp
    test_canvas.cpp
    test_test_pattern.cpp
    test_utf8.cpp
    test_text.cpp
    test_input_mapper.cpp
    test_simulator_platform.cpp
    test_json.cpp
    test_scene.cpp
    test_app.cpp
    test_config.cpp
    test_scroll.cpp
    test_notifications.cpp
    test_api.cpp
    test_memory_budget.cpp
    test_log.cpp
    test_frame_scheduler.cpp
    test_host.cpp
    test_clock.cpp
    test_icons.cpp
    test_web.cpp
    test_mqtt.cpp
    test_rotary_decoder.cpp
    test_mcu_protocol.cpp
)

target_include_directories(notrix_tests PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")

target_link_libraries(notrix_tests PRIVATE
    notrix_core
    notrix_imageio
    notrix_platform_simulator
    notrix_warnings)

# Golden fixtures are read from the source tree, not the build tree, so a failing
# test can write its diff PNGs next to the fixture it disagrees with.
target_compile_definitions(notrix_tests PRIVATE
    NOTRIX_TESTDATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/testdata")

if(MSVC)
    # The golden harness reads NOTRIX_UPDATE_GOLDEN / NOTRIX_STRICT_GOLDEN with
    # std::getenv, which MSVC deprecates in favour of the non-portable
    # _dupenv_s. This is host-only test code reading its own env vars, so the
    # portable call stays and the warning is suppressed here rather than in core.
    target_compile_definitions(notrix_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

add_test(NAME notrix_tests COMMAND notrix_tests)
