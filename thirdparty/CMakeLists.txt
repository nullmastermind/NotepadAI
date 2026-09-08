include(${CMAKE_SOURCE_DIR}/cmake/CPM.cmake)

CPMAddPackage(
    NAME ads
    GITHUB_REPOSITORY githubuser0xFFFF/Qt-Advanced-Docking-System
    GIT_TAG 34b68d6eab1556cf851d24e033909332771f3dfe
    OPTIONS
        "BUILD_STATIC ON"
        "BUILD_EXAMPLES OFF"
)

CPMAddPackage(
    NAME QSimpleUpdater
    GITHUB_REPOSITORY alex-spataru/QSimpleUpdater
    GIT_TAG 8e7017f7fbdc2b4b1a26ed1eef9ebcba6a50639c
    OPTIONS
        "QSIMPLE_UPDATER_BUILD_TESTS OFF"
)

set(QAPPLICATION_CLASS QApplication CACHE STRING "Inheritance class for SingleApplication" FORCE)
CPMAddPackage(
    NAME SingleApplication
    GITHUB_REPOSITORY itay-grudev/SingleApplication
    GIT_TAG 494772e98cef0aa88124f154feb575cc60b08b38
)

CPMAddPackage(
    NAME editorconfig_core_qt
    GITHUB_REPOSITORY editorconfig/editorconfig-core-qt
    GIT_TAG ab62f0554abf2bbe4d45427b36a8b2f81ca7b4ab
)

CPMAddPackage(
    NAME xxhash
    GITHUB_REPOSITORY Cyan4973/xxHash
    GIT_TAG v0.8.2
    DOWNLOAD_ONLY YES
)
if(xxhash_ADDED)
    add_library(xxhash INTERFACE)
    target_include_directories(xxhash SYSTEM INTERFACE ${xxhash_SOURCE_DIR})
    target_compile_definitions(xxhash INTERFACE XXH_INLINE_ALL XXH_NO_STREAM)
endif()

add_subdirectory(lua)
add_subdirectory(scintilla)
add_subdirectory(lexilla)
add_subdirectory(uchardet)
add_subdirectory(libvterm)
add_subdirectory(libptyqt)
add_subdirectory(xdiff)
add_subdirectory(md4c)
add_subdirectory(supertinycron)
add_subdirectory(miniz)

# SSH remote transport (vendored). mbedTLS MUST be added before libssh2 — the
# libssh2 wrapper consumes NN_MBEDTLS_INCLUDE_DIR + the mbedcrypto target that
# the mbedTLS wrapper publishes. Both expect their upstream release sources to
# be dropped into thirdparty/{mbedtls,libssh2}/<name>-<ver>/ (see each README).
add_subdirectory(mbedtls)
add_subdirectory(libssh2)

CPMAddPackage(
    NAME sentry
    GITHUB_REPOSITORY getsentry/sentry-native
    VERSION 0.14.2
    GIT_TAG 0.14.2
    OPTIONS
        "SENTRY_BACKEND inproc"
        "SENTRY_INTEGRATION_QT ON"
        "SENTRY_BUILD_EXAMPLES OFF"
        "SENTRY_BUILD_TESTS OFF"
    EXCLUDE_FROM_ALL YES
)

if(WIN32)
    add_subdirectory(webview2)
endif()

target_link_libraries(lexilla PRIVATE scintilla)
