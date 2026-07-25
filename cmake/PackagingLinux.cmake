message(STATUS "Configuring Linux packaging")

set(APPDIR ${CMAKE_BINARY_DIR}/AppDir)
set(APPDIR_USR ${APPDIR}/usr)

set(LINUXDEPLOY ${CMAKE_BINARY_DIR}/linuxdeploy-x86_64.AppImage)
set(LINUXDEPLOY_QT ${CMAKE_BINARY_DIR}/linuxdeploy-plugin-qt-x86_64.AppImage)

set(APPIMAGE_ENV_VARS
    LDAI_OUTPUT=NotepadAI-v${PROJECT_VERSION}-x86_64.AppImage
)

if(DEFINED ENV{QMAKE} AND NOT "$ENV{QMAKE}" STREQUAL "")
    set(APPIMAGE_QMAKE "$ENV{QMAKE}")
else()
    find_program(APPIMAGE_QMAKE NAMES qmake)
endif()

if(NOT APPIMAGE_QMAKE)
    message(FATAL_ERROR
        "Could not find qmake for AppImage packaging.\n"
        "Please install the QT 6 qmake or set the QMAKE variable to a Qt 6 qmake and re-run CMake, for example:\n"
        "  QMAKE=$(which qmake6) cmake -S . -B build -DAPP_DISTRIBUTION=AppImage"
    )
endif()

execute_process(
    COMMAND "${APPIMAGE_QMAKE}" -query QT_VERSION
    OUTPUT_VARIABLE APPIMAGE_QT_VERSION
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET
    RESULT_VARIABLE APPIMAGE_QMAKE_RESULT
)

if(NOT APPIMAGE_QMAKE_RESULT EQUAL 0 OR NOT APPIMAGE_QT_VERSION MATCHES "^6\\.")
    message(FATAL_ERROR
        "AppImage packaging requires a Qt 6 qmake, but CMake found:\n"
        "  ${APPIMAGE_QMAKE}\n"
        "Reported Qt version:\n"
        "  ${APPIMAGE_QT_VERSION}\n"
        "Please set the QMAKE variable to a Qt 6 qmake and re-run CMake, for example:\n"
        "  QMAKE=$(which qmake6) cmake -S . -B build -DAPP_DISTRIBUTION=AppImage"
    )
endif()

message(STATUS "Using qmake for AppImage packaging: ${APPIMAGE_QMAKE}")
list(APPEND APPIMAGE_ENV_VARS QMAKE=${APPIMAGE_QMAKE})

install(FILES
    ${PROJECT_SOURCE_DIR}/deploy/linux/NotepadAI.desktop
    DESTINATION share/applications
)
install(FILES
    ${PROJECT_SOURCE_DIR}/icon/NotepadAI.svg
    DESTINATION share/icons/hicolor/scalable/apps
)
install(FILES
    ${PROJECT_SOURCE_DIR}/icon/NotepadAI.svg
    DESTINATION share/icons/hicolor/scalable/mimetypes
)

add_custom_target(appdir
    COMMAND ${CMAKE_COMMAND}
        --install .
        --prefix ${APPDIR_USR}
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS NotepadAI
)

add_custom_target(download_linuxdeploy
    COMMAND wget --no-verbose -O ${LINUXDEPLOY}
        https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage
    COMMAND wget --no-verbose -O ${LINUXDEPLOY_QT}
        https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-x86_64.AppImage
    COMMAND chmod +x ${LINUXDEPLOY} ${LINUXDEPLOY_QT}
)

add_custom_target(appimage
    COMMAND ${CMAKE_COMMAND} -E env
        ${APPIMAGE_ENV_VARS}
        ${LINUXDEPLOY}
        --appdir ${APPDIR}
        --executable ${APPDIR_USR}/bin/NotepadAI
        --desktop-file ${APPDIR_USR}/share/applications/NotepadAI.desktop
        --icon-file ${APPDIR_USR}/share/icons/hicolor/scalable/apps/NotepadAI.svg
        --plugin qt
        --output appimage
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    DEPENDS appdir download_linuxdeploy
)

# ---------------------------------------------------------------------------
# CPack shared metadata
# ---------------------------------------------------------------------------
set(CPACK_PACKAGE_NAME "notepadai")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_CONTACT "NotepadAI Maintainers <maintainers@notepadai.app>")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "AI-powered text editor")
set(CPACK_PACKAGE_DESCRIPTION
    "NotepadAI is an AI-powered cross-platform text editor built with Qt6.")
set(CPACK_PACKAGE_FILE_NAME "NotepadAI-${PROJECT_VERSION}-Linux-x86_64")

# ---------------------------------------------------------------------------
# CPack DEB configuration
# ---------------------------------------------------------------------------
set(CPACK_DEBIAN_PACKAGE_ARCHITECTURE "amd64")
set(CPACK_DEBIAN_PACKAGE_MAINTAINER "NotepadAI Maintainers <maintainers@notepadai.app>")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/nullmastermind/NotepadAI")
set(CPACK_DEBIAN_PACKAGE_DEPENDS
    "libqt6core6 (>= 6.5), libqt6gui6 (>= 6.5), libqt6widgets6 (>= 6.5), libqt6network6 (>= 6.5), libqt6printsupport6 (>= 6.5), libqt6concurrent6 (>= 6.5), libxkbcommon0, libxcb-cursor0")
set(CPACK_DEBIAN_PACKAGE_RECOMMENDS "libsecret-1-0")
set(CPACK_DEBIAN_PACKAGE_SECTION "editors")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")

# ---------------------------------------------------------------------------
# CPack RPM configuration
# ---------------------------------------------------------------------------
set(CPACK_RPM_PACKAGE_ARCHITECTURE "x86_64")
set(CPACK_RPM_PACKAGE_LICENSE "GPL-3.0-or-later")
set(CPACK_RPM_PACKAGE_VENDOR "NotepadAI")
set(CPACK_RPM_PACKAGE_URL "https://github.com/nullmastermind/NotepadAI")
set(CPACK_RPM_PACKAGE_DESCRIPTION
    "NotepadAI is an AI-powered cross-platform text editor built with Qt6.")
set(CPACK_RPM_PACKAGE_REQUIRES "qt6-qtbase >= 6.5, libxkbcommon, libsecret")
set(CPACK_RPM_PACKAGE_GROUP "Applications/Editors")

include(CPack)

# Custom target: build .deb package
add_custom_target(deb
    COMMAND ${CMAKE_CPACK_COMMAND} -G DEB
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building DEB package"
)

# Custom target: build .rpm package
add_custom_target(rpm
    COMMAND ${CMAKE_CPACK_COMMAND} -G RPM
    WORKING_DIRECTORY ${CMAKE_BINARY_DIR}
    COMMENT "Building RPM package"
)
