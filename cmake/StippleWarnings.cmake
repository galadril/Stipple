# Compiler warning policy — blueprint §30.
#
# -Wall -Wextra -Wpedantic everywhere, promoted to errors in CI via
# -DSTIPPLE_WARNINGS_AS_ERRORS=ON. Applied through an INTERFACE target so the
# flags follow the core into every consumer (tests, emulator, device build).

add_library(stipple_warnings INTERFACE)

if(MSVC)
    target_compile_options(stipple_warnings INTERFACE
        /W4
        /permissive-          # strict standard conformance
        /utf-8                # source and execution charset
        $<$<BOOL:${STIPPLE_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(stipple_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wold-style-cast
        $<$<BOOL:${STIPPLE_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# Sanitizers are simulator/host-only; the device toolchain may not support them.
option(STIPPLE_SANITIZE "Enable ASan + UBSan on host builds" OFF)
if(STIPPLE_SANITIZE AND NOT MSVC AND NOT EMSCRIPTEN)
    target_compile_options(stipple_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(stipple_warnings INTERFACE -fsanitize=address,undefined)
endif()
