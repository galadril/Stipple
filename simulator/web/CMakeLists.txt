# Browser emulator — Emscripten only.
#
# This target is the thinnest possible shell around stipple_core. It adds no
# rendering logic of its own: the pixels the browser shows come from exactly the
# same code that will run on the TC002 (blueprint §24). If the emulator and the
# device ever disagree, the bug is in a platform adapter, never in two divergent
# renderers.

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "The emulator target requires the Emscripten toolchain. Use: cmake --preset emulator")
endif()

add_executable(stipple_emulator src/main.cpp)

# The simulator adapter supplies this build's half of the §53 boundary. The
# device build will link its TC002 counterpart here instead, and main.cpp will
# not change.
target_link_libraries(stipple_emulator PRIVATE stipple_core stipple_platform_simulator)

set_target_properties(stipple_emulator PROPERTIES
    OUTPUT_NAME "stipple-core"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/public")

target_link_options(stipple_emulator PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createStippleModule"
    "-sEXPORTED_FUNCTIONS=_stipple_init,_stipple_render,_stipple_framebuffer,_stipple_width,_stipple_height,_stipple_set_brightness,_stipple_set_wall_clock,_stipple_input,_stipple_app_count,_stipple_active_name,_stipple_active_duration_seconds,_stipple_dwell_millis,_stipple_is_paused,_stipple_showing_splash,_stipple_is_healthy,_stipple_notify,_stipple_notification_count,_stipple_notification_pending,_stipple_frames_rendered,_stipple_frames_skipped,_stipple_log_count,_stipple_log_line,_stipple_clock_theme_count,_stipple_clock_theme_name,_stipple_clock_theme,_stipple_set_clock_theme,_stipple_icon_staging,_stipple_icon_staging_capacity,_stipple_icon_id_buffer,_stipple_icon_id_capacity,_stipple_icon_commit,_stipple_icon_count,_stipple_icon_bytes_used,_stipple_show_icon_app,_stipple_http_request,_stipple_http_body,_stipple_http_body_length,_stipple_http_content_type,_stipple_input_source"
    "-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString,ccall"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sENVIRONMENT=web"
    "-sFILESYSTEM=0")
