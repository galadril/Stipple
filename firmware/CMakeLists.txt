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
