# miniz 3.0.2 (public domain) — amalgamated zip/deflate.
# Isolated as C so it does not enter the C++ PCH of NotepadAI.
add_library(miniz STATIC miniz.c miniz.h)
target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_SOURCE_DIR})
if(MSVC)
    target_compile_definitions(miniz PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
# miniz.c is C; keep C++ consumers from pulling a C PCH.
set_target_properties(miniz PROPERTIES
    C_STANDARD 99
    POSITION_INDEPENDENT_CODE ON
)
