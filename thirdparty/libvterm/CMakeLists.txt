# Project-local CMake shim for libvterm 0.3.3 (MIT).
# Source layout matches the upstream tarball: include/vterm.h + src/*.c

set(LIBVTERM_SOURCES
    src/encoding.c
    src/keyboard.c
    src/mouse.c
    src/parser.c
    src/pen.c
    src/screen.c
    src/state.c
    src/unicode.c
    src/vterm.c
)

add_library(libvterm STATIC ${LIBVTERM_SOURCES})

target_include_directories(libvterm
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/src
)

if(MSVC)
    target_compile_definitions(libvterm PRIVATE _CRT_SECURE_NO_WARNINGS)
    target_compile_options(libvterm PRIVATE /W0)
else()
    target_compile_options(libvterm PRIVATE -w)
endif()

set_target_properties(libvterm PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)
