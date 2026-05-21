# Minimal libptyqt static library (adapted from kafeg/ptyqt, MIT).
# Provides:
#  - IPtyProcess interface
#  - ConPtyProcess (Windows 10 1809+, no WinPTY fallback)
#  - UnixPtyProcess (forkpty)

find_package(Qt6 REQUIRED COMPONENTS Core)
set(CMAKE_AUTOMOC ON)

set(LIBPTYQT_SOURCES
    iptyprocess.h
    ptyqt.h
    ptyqt.cpp
)

if(WIN32)
    list(APPEND LIBPTYQT_SOURCES
        conptyprocess.h
        conptyprocess.cpp
    )
else()
    list(APPEND LIBPTYQT_SOURCES
        unixptyprocess.h
        unixptyprocess.cpp
    )
endif()

add_library(libptyqt STATIC ${LIBPTYQT_SOURCES})

target_include_directories(libptyqt
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(libptyqt
    PUBLIC
        Qt6::Core
)

if(MSVC)
    target_compile_options(libptyqt PRIVATE /W0)
else()
    target_compile_options(libptyqt PRIVATE -w)
endif()

if(UNIX AND NOT APPLE)
    target_link_libraries(libptyqt PUBLIC util)
endif()
