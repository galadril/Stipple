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

target_link_libraries(notrix_emulator PRIVATE notrix_core)

set_target_properties(notrix_emulator PROPERTIES
    OUTPUT_NAME "notrix-core"
    SUFFIX ".js"
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}/public")

target_link_options(notrix_emulator PRIVATE
    "-sMODULARIZE=1"
    "-sEXPORT_NAME=createNotrixModule"
    "-sEXPORTED_FUNCTIONS=_notrix_init,_notrix_render,_notrix_framebuffer,_notrix_width,_notrix_height,_notrix_set_brightness"
    "-sEXPORTED_RUNTIME_METHODS=HEAPU8"
    "-sALLOW_MEMORY_GROWTH=1"
    "-sENVIRONMENT=web"
    "-sFILESYSTEM=0")
