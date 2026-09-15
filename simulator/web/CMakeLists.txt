# Browser emulator — Emscripten only.
#
# This target is the thinnest possible shell around notrix_core. It adds no
# rendering logic of its own: the pixels the browser shows come from exactly the
# same code that will run on the TC002 (blueprint §24). If the emulator and the
# device ever disagree, the bug is in a platform adapter, never in two divergent
# renderers.

if(NOT EMSCRIPTEN)
    message(FATAL_ERROR "The emulator target requires the Emscripten toolchain. Use: cmake --preset emulator")
endif()

add_executable(notrix_emulator src/main.cpp)

# The simulator adapter supplies this build's half of the §53 boundary. The
# device build will link its TC002 counterpart here instead, and main.cpp will
# not change.
target_link_libraries(notrix_emulator PRIVATE notrix_core notrix_platform_simulator)

set_target_properties(notrix_emulator PROPERTIES
    OUTPUT_NAME "notrix-core"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/public")

target_link_options(notrix_emulator PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createNotrixModule"
    "-sEXPORTED_FUNCTIONS=_notrix_init,_notrix_render,_notrix_framebuffer,_notrix_width,_notrix_height,_notrix_set_brightness,_notrix_set_wall_clock,_notrix_input,_notrix_app_count,_notrix_active_name,_notrix_active_duration_seconds,_notrix_dwell_millis,_notrix_is_paused,_notrix_showing_splash,_notrix_is_healthy,_notrix_notify,_notrix_notification_count,_notrix_notification_pending,_notrix_frames_rendered,_notrix_frames_skipped,_notrix_log_count,_notrix_log_line,_notrix_clock_theme_count,_notrix_clock_theme_name,_notrix_clock_theme,_notrix_set_clock_theme,_notrix_icon_staging,_notrix_icon_staging_capacity,_notrix_icon_id_buffer,_notrix_icon_id_capacity,_notrix_icon_commit,_notrix_icon_count,_notrix_icon_bytes_used,_notrix_show_icon_app"
    "-sEXPORTED_RUNTIME_METHODS=HEAPU8,UTF8ToString"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sENVIRONMENT=web"
    "-sFILESYSTEM=0")
