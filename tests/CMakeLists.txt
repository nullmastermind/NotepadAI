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
