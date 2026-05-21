# Notepad Next unit tests.
# Enabled via -DNOTEPADNEXT_BUILD_TESTS=ON

qt_standard_project_setup()

# --- Theme resolver: pure header, no Qt runtime needed beyond core enums ---
qt_add_executable(test_theme_resolver test_theme_resolver.cpp)
target_include_directories(test_theme_resolver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_theme_resolver PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
# ApplicationSettings.h is included transitively via ThemeResolver.h; we compile
# ApplicationSettings.cpp here so the linker resolves its Q_OBJECT vtable bits.
# ApplicationSettings.cpp #includes <QApplication> for a default-value lambda,
# hence the Qt6::Widgets dependency above.
target_sources(test_theme_resolver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
)
add_test(NAME ThemeResolver COMMAND test_theme_resolver)

# --- ApplicationSettings: theme default / set / get / signal ---
qt_add_executable(test_application_settings test_application_settings.cpp)
target_include_directories(test_application_settings PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_application_settings PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_application_settings PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
)
add_test(NAME ApplicationSettings COMMAND test_application_settings)

# --- PreferencesDialog theme-combo binding (replicates the dialog's binding code) ---
qt_add_executable(test_preferences_theme_binding test_preferences_theme_binding.cpp)
target_include_directories(test_preferences_theme_binding PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_preferences_theme_binding PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_preferences_theme_binding PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
)
add_test(NAME PreferencesThemeBinding COMMAND test_preferences_theme_binding)

# --- chooseStylesheetResource: pure picker, only depends on QString ---
qt_add_executable(test_stylesheet_resource test_stylesheet_resource.cpp)
target_include_directories(test_stylesheet_resource PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_stylesheet_resource PRIVATE Qt6::Core Qt6::Test)
add_test(NAME StylesheetResource COMMAND test_stylesheet_resource)

# --- TerminalCwdResolver: pure path-resolution logic for the Terminal menu actions ---
qt_add_executable(test_terminal_cwd_resolver test_terminal_cwd_resolver.cpp)
target_include_directories(test_terminal_cwd_resolver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_cwd_resolver PRIVATE Qt6::Core Qt6::Test)
target_sources(test_terminal_cwd_resolver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/TerminalCwdResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalCwdResolver.h
)
add_test(NAME TerminalCwdResolver COMMAND test_terminal_cwd_resolver)

# --- TerminalColorScheme: light/dark palette validity ---
qt_add_executable(test_terminal_color_scheme test_terminal_color_scheme.cpp)
target_include_directories(test_terminal_color_scheme PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(test_terminal_color_scheme PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_sources(test_terminal_color_scheme PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalColorScheme.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalColorScheme.h
)
add_test(NAME TerminalColorScheme COMMAND test_terminal_color_scheme)

# --- ApplicationSettings terminal slice: shellCommand / terminalFont accessors and signals ---
qt_add_executable(test_application_settings_terminal test_application_settings_terminal.cpp)
target_include_directories(test_application_settings_terminal PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_application_settings_terminal PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_application_settings_terminal PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
)
add_test(NAME ApplicationSettingsTerminal COMMAND test_application_settings_terminal)

# --- ScrollbackBuffer: bounded history ring used by TerminalWidget for sb_pushline/sb_popline ---
qt_add_executable(test_scrollback_buffer test_scrollback_buffer.cpp)
target_include_directories(test_scrollback_buffer PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(test_scrollback_buffer PRIVATE Qt6::Core Qt6::Test libvterm)
target_sources(test_scrollback_buffer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/ScrollbackBuffer.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/ScrollbackBuffer.h
)
add_test(NAME ScrollbackBuffer COMMAND test_scrollback_buffer)

# --- TerminalCellSource selection helper: scrollback-aware extractSelectionText ---
qt_add_executable(test_terminal_selection test_terminal_selection.cpp)
target_include_directories(test_terminal_selection PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(test_terminal_selection PRIVATE Qt6::Core Qt6::Gui Qt6::Test libvterm)
target_sources(test_terminal_selection PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalCellSource.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalCellSource.h
)
add_test(NAME TerminalSelection COMMAND test_terminal_selection)
