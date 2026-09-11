# Compiler warning policy — blueprint §30.
#
# -Wall -Wextra -Wpedantic everywhere, promoted to errors in CI via
# -DNOTRIX_WARNINGS_AS_ERRORS=ON. Applied through an INTERFACE target so the
# flags follow the core into every consumer (tests, emulator, device build).

add_library(notrix_warnings INTERFACE)

if(MSVC)
    target_compile_options(notrix_warnings INTERFACE
        /W4
        /permissive-          # strict standard conformance
        /utf-8                # source and execution charset
        $<$<BOOL:${NOTRIX_WARNINGS_AS_ERRORS}>:/WX>)
else()
    target_compile_options(notrix_warnings INTERFACE
        -Wall
        -Wextra
        -Wpedantic
        -Wshadow
        -Wconversion
        -Wsign-conversion
        -Wold-style-cast
        $<$<BOOL:${NOTRIX_WARNINGS_AS_ERRORS}>:-Werror>)
endif()

# Sanitizers are simulator/host-only; the device toolchain may not support them.
option(NOTRIX_SANITIZE "Enable ASan + UBSan on host builds" OFF)
if(NOTRIX_SANITIZE AND NOT MSVC AND NOT EMSCRIPTEN)
    target_compile_options(notrix_warnings INTERFACE -fsanitize=address,undefined -fno-omit-frame-pointer)
    target_link_options(notrix_warnings INTERFACE -fsanitize=address,undefined)
endif()
