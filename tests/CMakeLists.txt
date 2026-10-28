# NotepadAI unit tests.
# Enabled via -DNOTEPADAI_BUILD_TESTS=ON

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

# --- PreferencesDialog AI format-combo binding (replicates the dialog's binding) ---
qt_add_executable(test_preferences_ai_format_binding test_preferences_ai_format_binding.cpp)
target_include_directories(test_preferences_ai_format_binding PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_preferences_ai_format_binding PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_preferences_ai_format_binding PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
)
add_test(NAME PreferencesAiFormatBinding COMMAND test_preferences_ai_format_binding)

# --- PreferencesPendingEdits: widget edits stay local until apply() ---
qt_add_executable(test_preferences_pending_edits test_preferences_pending_edits.cpp)
target_include_directories(test_preferences_pending_edits PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_preferences_pending_edits PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_preferences_pending_edits PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
)
add_test(NAME PreferencesPendingEdits COMMAND test_preferences_pending_edits)

# --- UnfocusedWheelFilter: combo/spin ignore wheel unless focused ---
qt_add_executable(test_unfocused_wheel_filter test_unfocused_wheel_filter.cpp)
target_include_directories(test_unfocused_wheel_filter PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_unfocused_wheel_filter PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
add_test(NAME UnfocusedWheelFilter COMMAND test_unfocused_wheel_filter)

# --- chooseStylesheetResource: pure picker, only depends on QString ---
qt_add_executable(test_stylesheet_resource test_stylesheet_resource.cpp)
target_include_directories(test_stylesheet_resource PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_stylesheet_resource PRIVATE Qt6::Core Qt6::Test)
add_test(NAME StylesheetResource COMMAND test_stylesheet_resource)

# --- DarkPalette: pure foreground-lightening transform for dark editor bg.
#     Header-only, so we only need the test source itself. ---
qt_add_executable(test_dark_palette test_dark_palette.cpp)
target_include_directories(test_dark_palette PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dark_palette PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME DarkPalette COMMAND test_dark_palette)

# --- CommitSubmitPolicy: empty-message ACP picker vs normal commit enable ---
qt_add_executable(test_commit_submit_policy test_commit_submit_policy.cpp)
target_include_directories(test_commit_submit_policy PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_commit_submit_policy PRIVATE Qt6::Core Qt6::Test)
add_test(NAME CommitSubmitPolicy COMMAND test_commit_submit_policy)

# --- GitRefreshCoalescer: trailing debounce + max-wait for status refresh ---
qt_add_executable(test_git_refresh_coalescer test_git_refresh_coalescer.cpp)
target_include_directories(test_git_refresh_coalescer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_git_refresh_coalescer PRIVATE Qt6::Core Qt6::Test)
add_test(NAME GitRefreshCoalescer COMMAND test_git_refresh_coalescer)

# --- ForceRemovePath: unlink Windows reserved names (NUL/CON/…) that git clean cannot ---
qt_add_executable(test_force_remove_path test_force_remove_path.cpp)
target_include_directories(test_force_remove_path PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_force_remove_path PRIVATE Qt6::Core Qt6::Test)
target_sources(test_force_remove_path PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/ForceRemovePath.cpp
    ${CMAKE_SOURCE_DIR}/src/git/ForceRemovePath.h
    ${CMAKE_SOURCE_DIR}/src/git/GitErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/git/GitError.h
)
add_test(NAME ForceRemovePath COMMAND test_force_remove_path)

# --- TerminalCwdResolver: pure path-resolution logic for the Terminal menu actions ---
qt_add_executable(test_terminal_cwd_resolver test_terminal_cwd_resolver.cpp)
target_include_directories(test_terminal_cwd_resolver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_cwd_resolver PRIVATE Qt6::Core Qt6::Test)
target_sources(test_terminal_cwd_resolver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/TerminalCwdResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalCwdResolver.h
    ${CMAKE_SOURCE_DIR}/src/remote/ExecutionContext.h
)
add_test(NAME TerminalCwdResolver COMMAND test_terminal_cwd_resolver)

# --- WorkspaceTaskScanner: task-file parsers behind the task runner gutter ---
qt_add_executable(test_workspace_task_scanner test_workspace_task_scanner.cpp)
target_include_directories(test_workspace_task_scanner PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_workspace_task_scanner PRIVATE Qt6::Core Qt6::Test)
target_sources(test_workspace_task_scanner PRIVATE
    ${CMAKE_SOURCE_DIR}/src/WorkspaceTaskScanner.cpp
    ${CMAKE_SOURCE_DIR}/src/WorkspaceTaskScanner.h
)
add_test(NAME WorkspaceTaskScanner COMMAND test_workspace_task_scanner)

# --- TerminalEmptyPanelPolicy: pure show/hide decision for the empty panel ---
qt_add_executable(test_terminal_empty_panel_policy test_terminal_empty_panel_policy.cpp)
target_include_directories(test_terminal_empty_panel_policy PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_empty_panel_policy PRIVATE Qt6::Core Qt6::Test)
target_sources(test_terminal_empty_panel_policy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/TerminalEmptyPanelPolicy.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalEmptyPanelPolicy.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
)
add_test(NAME TerminalEmptyPanelPolicy COMMAND test_terminal_empty_panel_policy)

# --- TerminalEmptyPanel: widget rebuild and row geometry ---
qt_add_executable(test_terminal_empty_panel test_terminal_empty_panel.cpp)
target_include_directories(test_terminal_empty_panel PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_terminal_empty_panel PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_terminal_empty_panel PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalEmptyPanel.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalEmptyPanel.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalIcons.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalIcons.h
)
add_test(NAME TerminalEmptyPanel COMMAND test_terminal_empty_panel)
set_tests_properties(TerminalEmptyPanel PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
# --- TerminalProcessStats: PTY process-tree CPU/RAM aggregation ---
qt_add_executable(test_terminal_process_stats test_terminal_process_stats.cpp)
target_include_directories(test_terminal_process_stats PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_process_stats PRIVATE Qt6::Core Qt6::Test)
target_sources(test_terminal_process_stats PRIVATE
    ${CMAKE_SOURCE_DIR}/src/TerminalProcessStats.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalProcessStats.h
)
add_test(NAME TerminalProcessStats COMMAND test_terminal_process_stats)


# --- TerminalSearchSession: sliced history scan, incremental pass, regex guard ---
qt_add_executable(test_terminal_search_session test_terminal_search_session.cpp)
target_include_directories(test_terminal_search_session PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_terminal_search_session PRIVATE Qt6::Core Qt6::Gui Qt6::Test libvterm)
target_sources(test_terminal_search_session PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearchSession.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearchSession.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearch.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearch.h
    ${CMAKE_SOURCE_DIR}/src/widgets/ScrollbackBuffer.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/ScrollbackBuffer.h
)
add_test(NAME TerminalSearchSession COMMAND test_terminal_search_session)


# --- TerminalGlyphCache tiles: same pixels as drawing each glyph through Qt ---
qt_add_executable(test_terminal_glyph_tiles test_terminal_glyph_tiles.cpp)
target_include_directories(test_terminal_glyph_tiles PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_terminal_glyph_tiles PRIVATE Qt6::Core Qt6::Gui Qt6::Test libvterm)
target_sources(test_terminal_glyph_tiles PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalGlyphCache.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalGlyphCache.h
)
add_test(NAME TerminalGlyphTiles COMMAND test_terminal_glyph_tiles)
set_tests_properties(TerminalGlyphTiles PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")


# --- terminalTabTitle: cwd as the ADS tab label, never the PTY process image ---
qt_add_executable(test_terminal_tab_title test_terminal_tab_title.cpp)
target_include_directories(test_terminal_tab_title PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_tab_title PRIVATE Qt6::Core Qt6::Test)
add_test(NAME TerminalTabTitle COMMAND test_terminal_tab_title)

# --- TerminalTaskLauncher: per-shell task wrapping that reports the exit code ---
qt_add_executable(test_terminal_task_launcher test_terminal_task_launcher.cpp)
target_include_directories(test_terminal_task_launcher PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_task_launcher PRIVATE Qt6::Core Qt6::Test)
target_sources(test_terminal_task_launcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskLauncher.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskLauncher.h
)
add_test(NAME TerminalTaskLauncher COMMAND test_terminal_task_launcher)

# --- Pm2Entrypoint: generated .cjs round-trips its definition; runs the command with env/cwd ---
qt_add_executable(test_pm2_entrypoint test_pm2_entrypoint.cpp)
target_include_directories(test_pm2_entrypoint PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/pm2)
target_link_libraries(test_pm2_entrypoint PRIVATE Qt6::Core Qt6::Test)
target_sources(test_pm2_entrypoint PRIVATE
    ${CMAKE_SOURCE_DIR}/src/DotEnv.cpp
    ${CMAKE_SOURCE_DIR}/src/DotEnv.h
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Entrypoint.cpp
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Entrypoint.h
)
add_test(NAME Pm2Entrypoint COMMAND test_pm2_entrypoint)

# --- Pm2Protocol: helper JSON lines, uptime, dump guard, names, logs command, .env ---
qt_add_executable(test_pm2_protocol test_pm2_protocol.cpp)
target_include_directories(test_pm2_protocol PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/pm2)
target_link_libraries(test_pm2_protocol PRIVATE Qt6::Core Qt6::Test)
target_sources(test_pm2_protocol PRIVATE
    ${CMAKE_SOURCE_DIR}/src/DotEnv.cpp
    ${CMAKE_SOURCE_DIR}/src/DotEnv.h
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Entrypoint.cpp
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Entrypoint.h
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskLauncher.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskLauncher.h
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Protocol.h
)
add_test(NAME Pm2Protocol COMMAND test_pm2_protocol)

# --- Pm2ProcessModel: list refreshes edit rows in place keyed by pm_id ---
qt_add_executable(test_pm2_process_model test_pm2_process_model.cpp)
target_include_directories(test_pm2_process_model PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/pm2)
target_link_libraries(test_pm2_process_model PRIVATE Qt6::Core Qt6::Test)
target_sources(test_pm2_process_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src/DotEnv.cpp
    ${CMAKE_SOURCE_DIR}/src/DotEnv.h
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Entrypoint.cpp
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Entrypoint.h
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskLauncher.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskLauncher.h
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Protocol.cpp
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2Protocol.h
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2ProcessModel.cpp
    ${CMAKE_SOURCE_DIR}/src/pm2/Pm2ProcessModel.h
)
add_test(NAME Pm2ProcessModel COMMAND test_pm2_process_model)

# --- AiDockGroup: project-group key / title / objectName / compact-after-close ---
qt_add_executable(test_ai_dock_group test_ai_dock_group.cpp)
target_include_directories(test_ai_dock_group PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ai_dock_group PRIVATE Qt6::Core Qt6::Test)
add_test(NAME AiDockGroup COMMAND test_ai_dock_group)

# --- AiDockSessionStrip: click / middle-click / bold current / [1][2][3] labels ---
qt_add_executable(test_ai_dock_session_strip test_ai_dock_session_strip.cpp)
target_include_directories(test_ai_dock_session_strip PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_ai_dock_session_strip PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_ai_dock_session_strip PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockSessionStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockSessionStrip.h
)
add_test(NAME AiDockSessionStrip COMMAND test_ai_dock_session_strip)

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

if(WIN32)
    # Job commit ceiling: a child that would commit past the cap must fail the
    # allocation. Links libptyqt for createCommitLimitedJob.
    qt_add_executable(test_child_commit_job test_child_commit_job.cpp)
    target_include_directories(test_child_commit_job PRIVATE
        ${CMAKE_SOURCE_DIR}/thirdparty/libptyqt)
    target_link_libraries(test_child_commit_job PRIVATE Qt6::Core Qt6::Test libptyqt psapi)
    add_test(NAME ChildCommitJob COMMAND test_child_commit_job)
endif()

# --- TerminalTaskRegistry: addTask/tasksForWorkspace round-trip + path normalization ---
qt_add_executable(test_terminal_task_registry test_terminal_task_registry.cpp)
target_include_directories(test_terminal_task_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_task_registry PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_terminal_task_registry PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskRegistry.h
)
add_test(NAME TerminalTaskRegistry COMMAND test_terminal_task_registry)

# --- EditTasksDialog: reorder must not clobber neighbor task fields ---
qt_add_executable(test_edit_tasks_dialog test_edit_tasks_dialog.cpp)
target_include_directories(test_edit_tasks_dialog PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
)
target_link_libraries(test_edit_tasks_dialog PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_edit_tasks_dialog PRIVATE
    ${CMAKE_SOURCE_DIR}/src/dialogs/EditTasksDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/EditTasksDialog.h
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskRegistry.h
)
add_test(NAME EditTasksDialog COMMAND test_edit_tasks_dialog)

# --- EditMiniAppsDialog: timeout survives Advanced off; random port is not 922x ---
qt_add_executable(test_edit_miniapps_dialog test_edit_miniapps_dialog.cpp)
target_include_directories(test_edit_miniapps_dialog PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
)
target_link_libraries(test_edit_miniapps_dialog PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
target_sources(test_edit_miniapps_dialog PRIVATE
    ${CMAKE_SOURCE_DIR}/src/dialogs/EditMiniAppsDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/EditMiniAppsDialog.h
    ${CMAKE_SOURCE_DIR}/src/MiniAppRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/MiniAppRegistry.h
    ${CMAKE_SOURCE_DIR}/src/MiniAppDefinition.h
    ${CMAKE_SOURCE_DIR}/src/MiniAppMenuLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/MiniAppMenuLayout.h
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
)
add_test(NAME EditMiniAppsDialog COMMAND test_edit_miniapps_dialog)

# --- MiniAppMenuLayout: group A-Z then ungrouped ---
qt_add_executable(test_mini_app_menu_layout test_mini_app_menu_layout.cpp)
target_include_directories(test_mini_app_menu_layout PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_mini_app_menu_layout PRIVATE Qt6::Core Qt6::Test)
target_sources(test_mini_app_menu_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/src/MiniAppMenuLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/MiniAppMenuLayout.h
)
add_test(NAME MiniAppMenuLayout COMMAND test_mini_app_menu_layout)

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

# --- TerminalRowLayout: palette resolution + run/background batching for the renderer ---
qt_add_executable(test_terminal_row_layout test_terminal_row_layout.cpp)
target_include_directories(test_terminal_row_layout PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(test_terminal_row_layout PRIVATE Qt6::Core Qt6::Gui Qt6::Test libvterm)
target_sources(test_terminal_row_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalRowLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalRowLayout.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalColorScheme.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalColorScheme.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalContrast.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalContrast.h
)
add_test(NAME TerminalRowLayout COMMAND test_terminal_row_layout)

# --- TerminalContrast: APCA minimum text contrast and its use in row layout ---
qt_add_executable(test_terminal_contrast test_terminal_contrast.cpp)
target_include_directories(test_terminal_contrast PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(test_terminal_contrast PRIVATE Qt6::Core Qt6::Gui Qt6::Test libvterm)
target_sources(test_terminal_contrast PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalContrast.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalContrast.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalRowLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalRowLayout.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalColorScheme.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalColorScheme.h
)
add_test(NAME TerminalContrast COMMAND test_terminal_contrast)

# --- TerminalLinkDetector: URLs and path:line:col under the pointer ---
qt_add_executable(test_terminal_links test_terminal_links.cpp)
target_include_directories(test_terminal_links PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(test_terminal_links PRIVATE Qt6::Core Qt6::Test)
target_sources(test_terminal_links PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalLinkDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalLinkDetector.h
)
add_test(NAME TerminalLinks COMMAND test_terminal_links)

# --- TerminalSearch: query compilation, row matching, scrollback line ids ---
qt_add_executable(test_terminal_search test_terminal_search.cpp)
target_include_directories(test_terminal_search PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(test_terminal_search PRIVATE Qt6::Core Qt6::Test libvterm)
target_sources(test_terminal_search PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearch.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearch.h
)
add_test(NAME TerminalSearch COMMAND test_terminal_search)

# --- TerminalBoxDrawing: procedural box/block/Powerline glyphs tile without seams ---
qt_add_executable(test_terminal_box_drawing test_terminal_box_drawing.cpp)
target_include_directories(test_terminal_box_drawing PRIVATE ${CMAKE_SOURCE_DIR}/src/widgets)
target_link_libraries(test_terminal_box_drawing PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_sources(test_terminal_box_drawing PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalBoxDrawing.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalBoxDrawing.h
)
add_test(NAME TerminalBoxDrawing COMMAND test_terminal_box_drawing)
set_tests_properties(TerminalBoxDrawing PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- TerminalAiHelper: wrapInCodeblock fenced codeblock wrapping ---
qt_add_executable(test_terminal_send_to_ai test_terminal_send_to_ai.cpp)
target_include_directories(test_terminal_send_to_ai PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_terminal_send_to_ai PRIVATE Qt6::Core Qt6::Test)
target_sources(test_terminal_send_to_ai PRIVATE
    ${CMAKE_SOURCE_DIR}/src/TerminalAiHelper.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalAiHelper.h
)
add_test(NAME TerminalSendToAi COMMAND test_terminal_send_to_ai)

# --- TerminalWidget key routing: regression for Tab/Backtab being swallowed by
#     QWidget::event() focus traversal before reaching keyPressEvent.
#     Subclasses TerminalWidget to inspect the routing without starting a PTY. ---
qt_add_executable(test_terminal_key_routing test_terminal_key_routing.cpp)
target_include_directories(test_terminal_key_routing PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_terminal_key_routing PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test Qt6::Concurrent
    libvterm
    libptyqt
)
target_sources(test_terminal_key_routing PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalCellSource.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalCellSource.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalColorScheme.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalColorScheme.h
    ${CMAKE_SOURCE_DIR}/src/widgets/ScrollbackBuffer.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/ScrollbackBuffer.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalRowLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalRowLayout.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalBoxDrawing.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalBoxDrawing.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalGlyphCache.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalGlyphCache.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalContrast.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalContrast.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalScrollBar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalScrollBar.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalLinkDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalLinkDetector.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearch.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearch.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearchSession.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearchSession.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearchBar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalSearchBar.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalIcons.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalIcons.h
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskLauncher.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalTaskLauncher.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
)
add_test(NAME TerminalKeyRouting COMMAND test_terminal_key_routing)
set_tests_properties(TerminalKeyRouting PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- AcpAgentRegistry: builtin seed, user CRUD, default + auto-approve policy ---
qt_add_executable(test_acp_agent_registry test_acp_agent_registry.cpp)
target_include_directories(test_acp_agent_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_agent_registry PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_agent_registry PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
)
add_test(NAME AcpAgentRegistry COMMAND test_acp_agent_registry)

# --- AcpAgentBrand: bundled icon key from explicit icon or id/name/command/args ---
qt_add_executable(test_acp_agent_brand test_acp_agent_brand.cpp)
target_include_directories(test_acp_agent_brand PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_agent_brand PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_agent_brand PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpAgentBrand.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
)
add_test(NAME AcpAgentBrand COMMAND test_acp_agent_brand)

# --- ClaudeSessionFiles: Claude Code session locate / freshness / copy (Switch agent) ---
qt_add_executable(test_claude_session_files test_claude_session_files.cpp)
target_include_directories(test_claude_session_files PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_claude_session_files PRIVATE Qt6::Core Qt6::Test)
target_sources(test_claude_session_files PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ClaudeSessionFiles.cpp
    ${CMAKE_SOURCE_DIR}/src/ClaudeSessionFiles.h
)
add_test(NAME ClaudeSessionFiles COMMAND test_claude_session_files)

# --- AcpSwitchAgent: idle gate, eligible targets, transcript freshness ---
qt_add_executable(test_acp_switch_agent test_acp_switch_agent.cpp)
target_include_directories(test_acp_switch_agent PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_switch_agent PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_switch_agent PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpSwitchAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSwitchAgent.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentBrand.h
)
add_test(NAME AcpSwitchAgent COMMAND test_acp_switch_agent)

# --- AcpOpenSessions: persisted open-session list for restored sessions ---
qt_add_executable(test_acp_open_sessions test_acp_open_sessions.cpp)
target_include_directories(test_acp_open_sessions PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_open_sessions PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_open_sessions PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpOpenSessions.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpOpenSessions.h
)
add_test(NAME AcpOpenSessions COMMAND test_acp_open_sessions)

# --- AcpPrewalk: switch to the prewalk model at the first edit, back at turn end ---
qt_add_executable(test_acp_prewalk test_acp_prewalk.cpp)
target_include_directories(test_acp_prewalk PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_prewalk PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_prewalk PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpPrewalk.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpPrewalk.h
)
add_test(NAME AcpPrewalk COMMAND test_acp_prewalk)

# --- AcpProtocol::acpExtractFrames: stdio newline-delimited JSON framing ---
qt_add_executable(test_acp_jsonrpc_framing test_acp_jsonrpc_framing.cpp)
target_include_directories(test_acp_jsonrpc_framing PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_jsonrpc_framing PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_jsonrpc_framing PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
)
add_test(NAME AcpJsonRpcFraming COMMAND test_acp_jsonrpc_framing)

# --- AcpProtocol::pickAutoApproveOptionId: pure auto-approve policy picker ---
qt_add_executable(test_acp_permission_policy test_acp_permission_policy.cpp)
target_include_directories(test_acp_permission_policy PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_permission_policy PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_permission_policy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
)
add_test(NAME AcpPermissionPolicy COMMAND test_acp_permission_policy)

# --- AcpErrorClassifier: classify / loginHint / friendlyMessage ---
qt_add_executable(test_acp_error_classifier test_acp_error_classifier.cpp)
target_include_directories(test_acp_error_classifier PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_error_classifier PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_error_classifier PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
)
add_test(NAME AcpErrorClassifier COMMAND test_acp_error_classifier)

# --- AcpProtocol::pathIsInsideWorkingDir: hermetic boundary check ---
qt_add_executable(test_acp_path_sandbox test_acp_path_sandbox.cpp)
target_include_directories(test_acp_path_sandbox PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_path_sandbox PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_path_sandbox PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
)
add_test(NAME AcpPathSandbox COMMAND test_acp_path_sandbox)

# --- AcpProtocol::buildSpawnArgv: POSIX/Windows program+args selection ---
qt_add_executable(test_acp_spawn_argv test_acp_spawn_argv.cpp)
target_include_directories(test_acp_spawn_argv PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_spawn_argv PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_spawn_argv PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
)
add_test(NAME AcpSpawnArgv COMMAND test_acp_spawn_argv)

# --- AcpProtocol JSON (de)serialization: content blocks + permission types ---
qt_add_executable(test_acp_protocol_serialization test_acp_protocol_serialization.cpp)
target_include_directories(test_acp_protocol_serialization PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_protocol_serialization PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_protocol_serialization PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
)
add_test(NAME AcpProtocolSerialization COMMAND test_acp_protocol_serialization)

# --- AcpScheduledJobs: live ScheduleWakeup / CronCreate job tracking ---
qt_add_executable(test_acp_scheduled_jobs test_acp_scheduled_jobs.cpp)
target_include_directories(test_acp_scheduled_jobs PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_scheduled_jobs PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_scheduled_jobs PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.h
)
add_test(NAME AcpScheduledJobs COMMAND test_acp_scheduled_jobs)

# --- AcpHistoryStore: debounced atomic per-session JSON writes ---
qt_add_executable(test_acp_history_store test_acp_history_store.cpp)
target_include_directories(test_acp_history_store PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_history_store PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_history_store PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
)
add_test(NAME AcpHistoryStore COMMAND test_acp_history_store)

# --- AcpSessionModel: in-memory transcript + persistence wiring ---
qt_add_executable(test_acp_session_model test_acp_session_model.cpp)
target_include_directories(test_acp_session_model PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_session_model PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_session_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
)
add_test(NAME AcpSessionModel COMMAND test_acp_session_model)

# --- AcpTranscriptTruncation: view-side 256-event cap, keep user/goal + last agent ---
qt_add_executable(test_acp_transcript_truncation test_acp_transcript_truncation.cpp)
target_include_directories(test_acp_transcript_truncation PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_transcript_truncation PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_transcript_truncation PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
)
add_test(NAME AcpTranscriptTruncation COMMAND test_acp_transcript_truncation)


# --- AcpSessionModel persistence integration: full restart round-trip
#     via the default QStandardPaths::AppDataLocation history directory. ---
qt_add_executable(test_acp_session_model_persistence_integration
    test_acp_session_model_persistence_integration.cpp)
target_include_directories(test_acp_session_model_persistence_integration PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_session_model_persistence_integration PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_session_model_persistence_integration PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
)
add_test(NAME AcpSessionModelPersistenceIntegration COMMAND test_acp_session_model_persistence_integration)

# --- GoalDraftGenerator: XML parse + transient connection teardown ---
qt_add_executable(test_goal_draft_generator test_goal_draft_generator.cpp)
target_include_directories(test_goal_draft_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_goal_draft_generator PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
target_sources(test_goal_draft_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.h
    ${CMAKE_SOURCE_DIR}/src/GoalDraftGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalDraftGenerator.h
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.cpp
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalDraftDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalDraftDialog.h
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
    ${CMAKE_SOURCE_DIR}/src/ai/IAnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Common.cpp
    ${CMAKE_SOURCE_DIR}/src/IAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.h
)
add_test(NAME GoalDraftGenerator COMMAND test_goal_draft_generator)
if(WIN32)
    target_sources(test_goal_draft_generator PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Windows.cpp)
    target_link_libraries(test_goal_draft_generator PRIVATE crypt32)
elseif(APPLE)
    target_sources(test_goal_draft_generator PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Mac.mm)
    target_link_libraries(test_goal_draft_generator PRIVATE
        "-framework Security" "-framework CoreFoundation")
else()
    target_sources(test_goal_draft_generator PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Linux.cpp)
endif()

# --- SendWithGoalDialog: persist and restore agent selection across dialog exits ---
qt_add_executable(test_send_with_goal_dialog test_send_with_goal_dialog.cpp)
target_include_directories(test_send_with_goal_dialog PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_send_with_goal_dialog PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_send_with_goal_dialog PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalAgentConfigDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalAgentConfigDialog.h
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalConfigWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalConfigWidget.h
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.cpp
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.h
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Common.cpp
)
add_test(NAME SendWithGoalDialog COMMAND test_send_with_goal_dialog)


# What AcpSessionView links beyond the ACP core: the prompt queue, Prewalk, the
# prompt improver with its LLM client, and the fold row's diff stat (link xdiff).
set(ACP_SESSION_VIEW_DEPS
    ${CMAKE_SOURCE_DIR}/src/AcpPrewalk.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpPrewalk.h
    ${CMAKE_SOURCE_DIR}/src/AcpPromptQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpPromptQueue.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPromptQueueStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPromptQueueStrip.h
    ${CMAKE_SOURCE_DIR}/src/ai/PromptImprover.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/PromptImprover.h
    ${CMAKE_SOURCE_DIR}/src/ai/LlmHttpClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/LlmHttpClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/SsePartialParser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/SsePartialParser.h
    ${CMAKE_SOURCE_DIR}/src/ai/OpenaiAnthropicBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/OpenaiAnthropicBridge.h
    ${CMAKE_SOURCE_DIR}/src/ai/ILlmHttpClient.h
    ${CMAKE_SOURCE_DIR}/src/PiAcpJudgeConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/PiAcpJudgeConfig.h
    ${CMAKE_SOURCE_DIR}/src/AcpDiffStat.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpDiffStat.h
    ${CMAKE_SOURCE_DIR}/src/git/BufferDiffEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/git/BufferDiffEngine.h
)

# Sources AcpMessageWidget.cpp now depends on (preprocessor + math renderer).
set(MARKDOWN_RENDER_SOURCES
    ${CMAKE_SOURCE_DIR}/src/markdown/MarkdownPreprocessor.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MarkdownPreprocessor.h
    ${CMAKE_SOURCE_DIR}/src/markdown/MathLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathLayout.h
    ${CMAKE_SOURCE_DIR}/src/markdown/MathParser.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathParser.h
    ${CMAKE_SOURCE_DIR}/src/markdown/MathRenderCache.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathRenderCache.h
    ${CMAKE_SOURCE_DIR}/src/widgets/MarkdownView.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/MarkdownView.h
)
# --- AcpAgentManager: lifecycle, history worker-thread placement, shutdown ---
qt_add_executable(test_acp_agent_manager test_acp_agent_manager.cpp)
target_include_directories(test_acp_agent_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
    ${CMAKE_SOURCE_DIR}/src/docks
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_agent_manager PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test
    SingleApplication::SingleApplication
    xdiff
)
target_sources(test_acp_agent_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeMcp.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeMcp.h
    ${CMAKE_SOURCE_DIR}/src/McpStdio.cpp
    ${CMAKE_SOURCE_DIR}/src/McpStdio.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/IAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${MARKDOWN_RENDER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEvents.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.h
    ${ACP_SESSION_VIEW_DEPS}
    ${CMAKE_SOURCE_DIR}/src/widgets/GrillMeFormWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GrillMeFormWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpTurnFoldRow.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpTurnFoldRow.h
    ${CMAKE_SOURCE_DIR}/src/AcpConversationCopy.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConversationCopy.h
    ${CMAKE_SOURCE_DIR}/src/AcpTurnFolding.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTurnFolding.h
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockSessionStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockSessionStrip.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.h
    ${CMAKE_SOURCE_DIR}/src/ai/IAnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Common.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalConfigWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalConfigWidget.h
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.cpp
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.h
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalDraftGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalDraftGenerator.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalDraftDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalDraftDialog.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.h
)
add_test(NAME AcpAgentManager COMMAND test_acp_agent_manager)

# --- AcpAgentManager exit + restart: W4 session state machine ---
qt_add_executable(test_acp_agent_exit_restart test_acp_agent_exit_restart.cpp)
target_include_directories(test_acp_agent_exit_restart PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
    ${CMAKE_SOURCE_DIR}/src/docks
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_agent_exit_restart PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test
    SingleApplication::SingleApplication
    xdiff
)
target_sources(test_acp_agent_exit_restart PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeMcp.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeMcp.h
    ${CMAKE_SOURCE_DIR}/src/McpStdio.cpp
    ${CMAKE_SOURCE_DIR}/src/McpStdio.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/IAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${MARKDOWN_RENDER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEvents.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.h
    ${ACP_SESSION_VIEW_DEPS}
    ${CMAKE_SOURCE_DIR}/src/widgets/GrillMeFormWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GrillMeFormWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpTurnFoldRow.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpTurnFoldRow.h
    ${CMAKE_SOURCE_DIR}/src/AcpConversationCopy.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConversationCopy.h
    ${CMAKE_SOURCE_DIR}/src/AcpTurnFolding.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTurnFolding.h
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockSessionStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockSessionStrip.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.h
    ${CMAKE_SOURCE_DIR}/src/ai/IAnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Common.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalConfigWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalConfigWidget.h
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.cpp
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.h
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalDraftGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalDraftGenerator.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalDraftDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalDraftDialog.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.h
)
add_test(NAME AcpAgentExitRestart COMMAND test_acp_agent_exit_restart)

# --- AiAgentDock: closeEvent prompt-while-running confirmation gate ---
qt_add_executable(test_acp_dock_close_event test_acp_dock_close_event.cpp)
target_include_directories(test_acp_dock_close_event PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
    ${CMAKE_SOURCE_DIR}/src/docks
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_dock_close_event PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test
    SingleApplication::SingleApplication
    xdiff
)
target_sources(test_acp_dock_close_event PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/IAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${MARKDOWN_RENDER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEvents.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.h
    ${CMAKE_SOURCE_DIR}/src/GrillMeMcp.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeMcp.h
    ${CMAKE_SOURCE_DIR}/src/McpStdio.cpp
    ${CMAKE_SOURCE_DIR}/src/McpStdio.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
    ${ACP_SESSION_VIEW_DEPS}
    ${CMAKE_SOURCE_DIR}/src/widgets/GrillMeFormWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GrillMeFormWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpTurnFoldRow.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpTurnFoldRow.h
    ${CMAKE_SOURCE_DIR}/src/AcpConversationCopy.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConversationCopy.h
    ${CMAKE_SOURCE_DIR}/src/AcpTurnFolding.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTurnFolding.h
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockSessionStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockSessionStrip.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.h
    ${CMAKE_SOURCE_DIR}/src/ai/IAnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Common.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalConfigWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalConfigWidget.h
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.cpp
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.h
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GoalCustomApiFields.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalDraftGenerator.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalDraftGenerator.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalDraftDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/GoalDraftDialog.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.h
)
add_test(NAME AcpDockCloseEvent COMMAND test_acp_dock_close_event)

# --- AcpSessionView driven by real wire frames (wake-up turns) ---
qt_add_executable(test_acp_session_view_wake test_acp_session_view_wake.cpp)
get_target_property(_acp_view_srcs test_acp_dock_close_event SOURCES)
list(FILTER _acp_view_srcs EXCLUDE REGEX "test_acp_dock_close_event\\.cpp$")
target_sources(test_acp_session_view_wake PRIVATE ${_acp_view_srcs})
target_include_directories(test_acp_session_view_wake PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
    ${CMAKE_SOURCE_DIR}/src/docks
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_session_view_wake PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test
    SingleApplication::SingleApplication
    xdiff
)
add_test(NAME AcpSessionViewWake COMMAND test_acp_session_view_wake)

foreach(_goal_http_tgt
        test_send_with_goal_dialog
        test_acp_agent_manager
        test_acp_agent_exit_restart
        test_acp_dock_close_event
        test_acp_session_view_wake)
    if(WIN32)
        target_sources(${_goal_http_tgt} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Windows.cpp)
        target_link_libraries(${_goal_http_tgt} PRIVATE crypt32)
    elseif(APPLE)
        target_sources(${_goal_http_tgt} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Mac.mm)
        target_link_libraries(${_goal_http_tgt} PRIVATE
            "-framework Security" "-framework CoreFoundation")
    else()
        target_sources(${_goal_http_tgt} PRIVATE
            ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Linux.cpp)
    endif()
endforeach()

# Switch agent + restored sessions: manager, dock and its agent button.
foreach(_acp_dock_tgt
        test_acp_agent_manager
        test_acp_agent_exit_restart
        test_acp_dock_close_event
        test_acp_session_view_wake)
    target_sources(${_acp_dock_tgt} PRIVATE
        ${CMAKE_SOURCE_DIR}/src/AcpAgentIcons.cpp
        ${CMAKE_SOURCE_DIR}/src/AcpAgentIcons.h
        ${CMAKE_SOURCE_DIR}/src/AcpOpenSessions.cpp
        ${CMAKE_SOURCE_DIR}/src/AcpOpenSessions.h
        ${CMAKE_SOURCE_DIR}/src/AcpSwitchAgent.cpp
        ${CMAKE_SOURCE_DIR}/src/AcpSwitchAgent.h
        ${CMAKE_SOURCE_DIR}/src/ClaudeSessionFiles.cpp
        ${CMAKE_SOURCE_DIR}/src/ClaudeSessionFiles.h
        ${CMAKE_SOURCE_DIR}/src/widgets/AiDockTitleBar.cpp
        ${CMAKE_SOURCE_DIR}/src/widgets/AiDockTitleBar.h)
endforeach()

# --- AiDockTitleBar: AI dock title bar with the Switch agent button ---
qt_add_executable(test_ai_dock_title_bar test_ai_dock_title_bar.cpp)
target_include_directories(test_ai_dock_title_bar PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ai_dock_title_bar PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_ai_dock_title_bar PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockTitleBar.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AiDockTitleBar.h
)
add_test(NAME AiDockTitleBar COMMAND test_ai_dock_title_bar)

# --- AcpImageAttachmentList: validation, queue, drag/drop ---
qt_add_executable(test_acp_image_attachment_limits test_acp_image_attachment_limits.cpp)
target_include_directories(test_acp_image_attachment_limits PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_image_attachment_limits PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_image_attachment_limits PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.h
)
add_test(NAME AcpImageAttachmentLimits COMMAND test_acp_image_attachment_limits)

# --- AcpUsageIndicator: label/bar visibility + warning/error levels ---
qt_add_executable(test_acp_usage_indicator test_acp_usage_indicator.cpp)
target_include_directories(test_acp_usage_indicator PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_usage_indicator PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_usage_indicator PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.h
)
add_test(NAME AcpUsageIndicator COMMAND test_acp_usage_indicator)

# --- ClaudeUsage: usage parsing/formatting rules + ClaudeUsageService (fake transport) ---
qt_add_executable(test_claude_usage test_claude_usage.cpp)
target_include_directories(test_claude_usage PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_claude_usage PRIVATE Qt6::Core Qt6::Gui Qt6::Network Qt6::Test)
target_sources(test_claude_usage PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ai/ClaudeUsage.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ClaudeUsage.h
    ${CMAKE_SOURCE_DIR}/src/ai/ClaudeUsageAccounts.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ClaudeUsageAccounts.h
    ${CMAKE_SOURCE_DIR}/src/ai/ClaudeUsageService.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/ClaudeUsageService.h
)
add_test(NAME ClaudeUsage COMMAND test_claude_usage)

# --- AcpPromptQueue: send-while-busy enqueue vs send-now / side-prompt ---
qt_add_executable(test_acp_prompt_queue test_acp_prompt_queue.cpp)
target_include_directories(test_acp_prompt_queue PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_prompt_queue PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_prompt_queue PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpPromptQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpPromptQueue.h
)
add_test(NAME AcpPromptQueue COMMAND test_acp_prompt_queue)

# --- AcpPromptQueueStrip: empty hide, preview, remove signal ---
qt_add_executable(test_acp_prompt_queue_strip test_acp_prompt_queue_strip.cpp)
target_include_directories(test_acp_prompt_queue_strip PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_prompt_queue_strip PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_prompt_queue_strip PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPromptQueueStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPromptQueueStrip.h
)
add_test(NAME AcpPromptQueueStrip COMMAND test_acp_prompt_queue_strip)

# --- AcpMessageWidget: streaming markdown, thought collapse, raw thought text ---
qt_add_executable(test_acp_message_widget_streaming test_acp_message_widget_streaming.cpp)
target_include_directories(test_acp_message_widget_streaming PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_message_widget_streaming PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_message_widget_streaming PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${MARKDOWN_RENDER_SOURCES}
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
)
add_test(NAME AcpMessageWidgetStreaming COMMAND test_acp_message_widget_streaming)

# --- AcpToolCallCard: lazy/coalesced tool-output body rendering ---
qt_add_executable(test_acp_tool_call_card test_acp_tool_call_card.cpp)
target_include_directories(test_acp_tool_call_card PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_tool_call_card PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_tool_call_card PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.h
    ${MARKDOWN_RENDER_SOURCES}
)
add_test(NAME AcpToolCallCard COMMAND test_acp_tool_call_card)

# --- ProfileScope: lock-free per-tag timing aggregation ---
qt_add_executable(test_profile_scope test_profile_scope.cpp)
target_include_directories(test_profile_scope PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_profile_scope PRIVATE Qt6::Core Qt6::Test)
target_sources(test_profile_scope PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
)
add_test(NAME ProfileScope COMMAND test_profile_scope)

# --- ShutdownDiagnostics: report formatting, event recording, file output ---
qt_add_executable(test_shutdown_diagnostics test_shutdown_diagnostics.cpp)
target_include_directories(test_shutdown_diagnostics PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_shutdown_diagnostics PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_shutdown_diagnostics PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ShutdownDiagnostics.cpp
    ${CMAKE_SOURCE_DIR}/src/ShutdownDiagnostics.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
)
target_compile_definitions(test_shutdown_diagnostics PRIVATE APP_VERSION="test")
add_test(NAME ShutdownDiagnostics COMMAND test_shutdown_diagnostics)

# --- GitDiffParser: unified diff → flat parallel arrays.
#     Hot-path parser; the 5k-line bench hard-fails > 60 ms so a regression in
#     the diff parser is caught at CI time, not at the user's keystroke. ---
qt_add_executable(test_git_diff_parser test_git_diff_parser.cpp)
target_include_directories(test_git_diff_parser PRIVATE ${CMAKE_SOURCE_DIR}/src/git)
target_link_libraries(test_git_diff_parser PRIVATE Qt6::Core Qt6::Test)
target_sources(test_git_diff_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.h
)
add_test(NAME GitDiffParser COMMAND test_git_diff_parser)

# --- DiffFileClassifier: which files of a diff start collapsed and why
#     (binary, lockfile, generated, deleted, large; first rule wins). ---
qt_add_executable(test_diff_file_classifier test_diff_file_classifier.cpp)
target_include_directories(test_diff_file_classifier PRIVATE ${CMAKE_SOURCE_DIR}/src/git)
target_link_libraries(test_diff_file_classifier PRIVATE Qt6::Core Qt6::Test)
target_sources(test_diff_file_classifier PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileClassifier.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.h
)
add_test(NAME DiffFileClassifier COMMAND test_diff_file_classifier)

# --- DiffDocumentBuilder: the diff view's buffer (file headers, hunks, body
#     rows, commit card, links, word spans, lexer slots) built off-thread.
#     Fragments for Load diff must equal the inline build. ---
qt_add_executable(test_diff_document_builder test_diff_document_builder.cpp)
target_include_directories(test_diff_document_builder PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
    ${CMAKE_SOURCE_DIR}/thirdparty/scintilla/include
    ${CMAKE_SOURCE_DIR}/thirdparty/lexilla/include
)
target_link_libraries(test_diff_document_builder PRIVATE Qt6::Core Qt6::Test lexilla)
target_sources(test_diff_document_builder PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/DiffDocument.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffDocumentBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffDocumentBuilder.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileClassifier.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffSyntaxMapper.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffSyntaxMapper.h
    ${CMAKE_SOURCE_DIR}/src/git/GitRelativeTime.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitRelativeTime.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
)
add_test(NAME DiffDocumentBuilder COMMAND test_diff_document_builder)

# --- DiffLanguages: language detection from a Lua snapshot (file name, Lua
#     name pattern, extension) and the per-document lexer style-slot
#     allocator (compaction, LexCPP inactive fold, overflow). ---
qt_add_executable(test_diff_languages test_diff_languages.cpp)
target_include_directories(test_diff_languages PRIVATE ${CMAKE_SOURCE_DIR}/src/git)
target_link_libraries(test_diff_languages PRIVATE Qt6::Core Qt6::Test)
target_sources(test_diff_languages PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/DiffLanguages.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffLanguages.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.h
)
add_test(NAME DiffLanguages COMMAND test_diff_languages)

# --- DiffFileTreeModel: the commit view's changed-files tree (directories
#     first, compacted chains, delegate roles, filter keeps ancestors). ---
qt_add_executable(test_diff_file_tree_model test_diff_file_tree_model.cpp)
target_include_directories(test_diff_file_tree_model PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/git)
target_link_libraries(test_diff_file_tree_model PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_sources(test_diff_file_tree_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileTreeModel.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileTreeModel.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.h
)
add_test(NAME DiffFileTreeModel COMMAND test_diff_file_tree_model)

# --- GitGutterMarkers: pure diff → editor-marker line mapping.
#     Verifies that hunk classification (Added vs Modified vs Deleted) and
#     line indexing land where the gutter painter expects them. ---
qt_add_executable(test_git_gutter_markers test_git_gutter_markers.cpp)
target_include_directories(test_git_gutter_markers PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git
    ${CMAKE_SOURCE_DIR}/src/decorators
    ${CMAKE_SOURCE_DIR}/thirdparty/xdiff
)
target_link_libraries(test_git_gutter_markers PRIVATE Qt6::Core Qt6::Test xdiff)
target_sources(test_git_gutter_markers PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/BufferDiffEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/git/BufferDiffEngine.h
    ${CMAKE_SOURCE_DIR}/src/decorators/GitGutterMarkers.cpp
    ${CMAKE_SOURCE_DIR}/src/decorators/GitGutterMarkers.h
)
add_test(NAME GitGutterMarkers COMMAND test_git_gutter_markers)

# --- BufferDiffEngine: xdl_diff wrapper for in-process buffer-vs-HEAD diff.
#     The 5k-line bench hard-fails > 60 ms so a regression in the hot path
#     (every keystroke can trigger a re-diff) is caught at CI time. ---
qt_add_executable(test_buffer_diff_engine test_buffer_diff_engine.cpp)
target_include_directories(test_buffer_diff_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git
    ${CMAKE_SOURCE_DIR}/thirdparty/xdiff
)
target_link_libraries(test_buffer_diff_engine PRIVATE Qt6::Core Qt6::Test xdiff)
target_sources(test_buffer_diff_engine PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/BufferDiffEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/git/BufferDiffEngine.h
)
add_test(NAME BufferDiffEngine COMMAND test_buffer_diff_engine)

# --- GitBaseBlobCache: app-wide LRU 16MB cache, xxHash key, mutex-guarded.
#     Verifies put/get round-trip, LRU eviction, MRU promotion on get,
#     per-repo invalidation, and budget shrink eviction. ---
qt_add_executable(test_git_base_blob_cache test_git_base_blob_cache.cpp)
target_include_directories(test_git_base_blob_cache PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_git_base_blob_cache PRIVATE Qt6::Core Qt6::Test xxhash)
target_sources(test_git_base_blob_cache PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitBaseBlobCache.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitBaseBlobCache.h
)
add_test(NAME GitBaseBlobCache COMMAND test_git_base_blob_cache)

# --- GitBlameParser: `git blame --porcelain` decoder.
#     Per-line author/time/summary extraction with sha-deduped records.
#     5k-line bench hard-fails > 60 ms (mirrors GitDiffParser budget). ---
qt_add_executable(test_git_blame_parser test_git_blame_parser.cpp)
target_include_directories(test_git_blame_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_git_blame_parser PRIVATE Qt6::Core Qt6::Test)
target_sources(test_git_blame_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitBlameParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitBlameParser.h
)
add_test(NAME GitBlameParser COMMAND test_git_blame_parser)

# --- EditorMinimap math: pure y ↔ line mapping used on the paint hot path
#     and every minimap mouse event. Header-only; the unit test verifies
#     clamping, round-trip identity, huge-file overflow safety, and a
#     hard-fail benchmark < 1 µs per call. ---
qt_add_executable(test_editor_minimap test_editor_minimap.cpp)
target_include_directories(test_editor_minimap PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_editor_minimap PRIVATE Qt6::Core Qt6::Test)
add_test(NAME EditorMinimap COMMAND test_editor_minimap)

# --- GitNumstatParser: porcelain `git diff --numstat -z` decoder
#     (normal, renamed triplet, binary `-\t-\t`, UTF-8 paths, truncated input). ---
qt_add_executable(test_git_numstat_parser test_git_numstat_parser.cpp)
target_include_directories(test_git_numstat_parser PRIVATE ${CMAKE_SOURCE_DIR}/src/git)
target_link_libraries(test_git_numstat_parser PRIVATE Qt6::Core Qt6::Test)
target_sources(test_git_numstat_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitNumstatParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitNumstatParser.h
)
add_test(NAME GitNumstatParser COMMAND test_git_numstat_parser)

# --- GitStatusSync + GitStatusModel: Git tab refresh path. Identical status
#     output is a no-op, parsing happens off the UI thread (latest wins), small
#     changes update rows in place (persistent indexes + numstat survive, no
#     reset), repeated numstat merges emit nothing. ---
qt_add_executable(test_git_status_sync test_git_status_sync.cpp)
target_include_directories(test_git_status_sync PRIVATE ${CMAKE_SOURCE_DIR}/src/git)
target_link_libraries(test_git_status_sync PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Test)
target_sources(test_git_status_sync PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusSync.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusSync.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusParser.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusModel.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusModel.h
    ${CMAKE_SOURCE_DIR}/src/git/GitNumstatParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitNumstatParser.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.h
)
add_test(NAME GitStatusSync COMMAND test_git_status_sync)

# --- GitRepoDiscovery: `git worktree list --porcelain` plus combo fallback. ---
qt_add_executable(test_git_repo_discovery test_git_repo_discovery.cpp)
target_include_directories(test_git_repo_discovery PRIVATE ${CMAKE_SOURCE_DIR}/src/git)
target_link_libraries(test_git_repo_discovery PRIVATE Qt6::Core Qt6::Test)
target_sources(test_git_repo_discovery PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitRepoDiscovery.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitRepoDiscovery.h
    ${CMAKE_SOURCE_DIR}/src/git/GitRepoInfo.h
    ${CMAKE_SOURCE_DIR}/src/git/GitRepoModel.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitRepoModel.h
    ${CMAKE_SOURCE_DIR}/src/git/GitErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/git/GitError.h
)
add_test(NAME GitRepoDiscovery COMMAND test_git_repo_discovery)

# --- GitDiffCache: byte-budget LRU keyed by xxHash3 of (repo, relPath, staged). ---
qt_add_executable(test_git_diff_cache test_git_diff_cache.cpp)
target_include_directories(test_git_diff_cache PRIVATE ${CMAKE_SOURCE_DIR}/src/git)
target_link_libraries(test_git_diff_cache PRIVATE Qt6::Core Qt6::Test xxhash)
target_sources(test_git_diff_cache PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffCache.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffCache.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.h
)
add_test(NAME GitDiffCache COMMAND test_git_diff_cache)

# --- GitDiffSyntaxMapper word-span alignment: positional contract between the
#     mapper's WordSpans and the painter's indicator placement, plus a UTF-8
#     boundary invariant. The previous N:M code concatenated paired blocks and
#     ran a single outer LCS — repeating per-line patterns mis-projected the
#     spans ~4 chars LEFT. The shift-best heuristic in pairAndEmit closes that.
qt_add_executable(test_git_diff_word_span_alignment test_git_diff_word_span_alignment.cpp)
target_include_directories(test_git_diff_word_span_alignment PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
    ${CMAKE_SOURCE_DIR}/thirdparty/scintilla/include
    ${CMAKE_SOURCE_DIR}/thirdparty/lexilla/include
)
target_link_libraries(test_git_diff_word_span_alignment PRIVATE
    Qt6::Core Qt6::Test lexilla
)
target_sources(test_git_diff_word_span_alignment PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffSyntaxMapper.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffSyntaxMapper.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
)
add_test(NAME GitDiffWordSpanAlignment COMMAND test_git_diff_word_span_alignment)

# --- GitLogParser: streaming \x1f/\x1e-separated git log records →
#     GitCommitInfo. Edge cases: chunk splits mid-field, mid-record, merge
#     commits with multiple parents, root commits with empty parents, UTF-8
#     author names, malformed records dropped silently. ---
qt_add_executable(test_git_log_parser test_git_log_parser.cpp)
target_include_directories(test_git_log_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_git_log_parser PRIVATE Qt6::Core Qt6::Test)
target_sources(test_git_log_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitLogParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitLogParser.h
    ${CMAKE_SOURCE_DIR}/src/git/GitCommitInfo.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
)
add_test(NAME GitLogParser COMMAND test_git_log_parser)

# --- GitHistoryModel: append / dedupe / clear / role accessors for the
#     QAbstractListModel powering the History tab's QListView. ---
qt_add_executable(test_git_history_model test_git_history_model.cpp)
target_include_directories(test_git_history_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_git_history_model PRIVATE Qt6::Core Qt6::Test)
target_sources(test_git_history_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/GitHistoryModel.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitHistoryModel.h
    ${CMAKE_SOURCE_DIR}/src/git/GitCommitInfo.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
)
add_test(NAME GitHistoryModel COMMAND test_git_history_model)

# --- PathStatusIndex: per-file + folder-rollup git status index for the
#     workspace file tree (Files tab decoration). Pure value object; tests
#     run as Qt6::Core + Qt6::Test only. Includes hard-fail benchmarks:
#       rebuild(1000 entries)                          < 1 ms
#       deltaPaths(1000 vs 1000, 5% divergence)        < 1 ms
#       1,000,000 mixed file+folder lookups            < 100 ms
#     Regressions in any benchmark hard-fail at CI time. ---
qt_add_executable(test_path_status_index test_path_status_index.cpp)
target_include_directories(test_path_status_index PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_path_status_index PRIVATE Qt6::Core Qt6::Test)
target_sources(test_path_status_index PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/PathStatusIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/git/PathStatusIndex.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusEntry.h
)
add_test(NAME PathStatusIndex COMMAND test_path_status_index)

# --- SubmoduleStatusFetcher: per-submodule `git status` fan-out lifecycle.
#     Regression for the delete-folder-in-submodule crash — a killed/crashed
#     QProcess emits BOTH errorOccurred and finished, so onTaskFinished must be
#     idempotent (no UAF / null deref, m_inflight decremented exactly once,
#     entriesReady emitted exactly once). Driven offline via the spawn-override
#     test seam (no real submodule or git binary required). ---
qt_add_executable(test_submodule_status_fetcher test_submodule_status_fetcher.cpp)
target_include_directories(test_submodule_status_fetcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_submodule_status_fetcher PRIVATE Qt6::Core Qt6::Test)
target_sources(test_submodule_status_fetcher PRIVATE
    ${CMAKE_SOURCE_DIR}/src/git/SubmoduleStatusFetcher.cpp
    ${CMAKE_SOURCE_DIR}/src/git/SubmoduleStatusFetcher.h
    ${CMAKE_SOURCE_DIR}/src/git/GitProcessRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitProcessRunner.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusParser.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusEntry.h
)
add_test(NAME SubmoduleStatusFetcher COMMAND test_submodule_status_fetcher)


# --- BrowserTabPin: Chrome-style pin hides title + close, keeps favicon ---
qt_add_executable(test_browser_tab_pin test_browser_tab_pin.cpp)
target_include_directories(test_browser_tab_pin PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_browser_tab_pin PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test qtadvanceddocking-qt6)
target_sources(test_browser_tab_pin PRIVATE
    ${CMAKE_SOURCE_DIR}/src/BrowserTabPin.cpp
    ${CMAKE_SOURCE_DIR}/src/BrowserTabPin.h
)
add_test(NAME BrowserTabPin COMMAND test_browser_tab_pin)

# --- DockTabReorder: public ADS addDockWidget(Center, index) keeps tab bar
#     and content stack in the same order. Real CDockManager, matching
#     test_dock_bottom_tool_tab. ---
qt_add_executable(test_dock_tab_reorder test_dock_tab_reorder.cpp)
target_include_directories(test_dock_tab_reorder PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dock_tab_reorder PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test qtadvanceddocking-qt6)
target_sources(test_dock_tab_reorder PRIVATE
    ${CMAKE_SOURCE_DIR}/src/DockTabReorder.cpp
    ${CMAKE_SOURCE_DIR}/src/DockTabReorder.h
    ${CMAKE_SOURCE_DIR}/src/BrowserTabPin.cpp
    ${CMAKE_SOURCE_DIR}/src/BrowserTabPin.h
)
add_test(NAME DockTabReorder COMMAND test_dock_tab_reorder)

# --- FaviconIcon: PNG/JPEG/ICO bytes → QIcon for browser tab icons ---
qt_add_executable(test_favicon_icon test_favicon_icon.cpp)
target_include_directories(test_favicon_icon PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_favicon_icon PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_favicon_icon PRIVATE
    ${CMAKE_SOURCE_DIR}/src/FaviconIcon.cpp
    ${CMAKE_SOURCE_DIR}/src/FaviconIcon.h
)
add_test(NAME FaviconIcon COMMAND test_favicon_icon)

# --- QuickBrowseUrl: file-path detection + https prefix for Quick Browse input ---
qt_add_executable(test_quick_browse_url test_quick_browse_url.cpp)
target_include_directories(test_quick_browse_url PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_quick_browse_url PRIVATE Qt6::Core Qt6::Test)
target_sources(test_quick_browse_url PRIVATE
    ${CMAKE_SOURCE_DIR}/src/QuickBrowseUrl.cpp
    ${CMAKE_SOURCE_DIR}/src/QuickBrowseUrl.h
)
add_test(NAME QuickBrowseUrl COMMAND test_quick_browse_url)

# --- BrowserProxyArgs: proxy arg builder unit tests ---
qt_add_executable(test_browser_proxy_args test_browser_proxy_args.cpp)
target_include_directories(test_browser_proxy_args PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_browser_proxy_args PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_browser_proxy_args PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/BrowserProxyArgs.cpp
    ${CMAKE_SOURCE_DIR}/src/BrowserProxyArgs.h
)
add_test(NAME BrowserProxyArgs COMMAND test_browser_proxy_args)

# --- CronExpression: parse, nextFireTime, nextFireTimes ---
qt_add_executable(test_cron_expression test_cron_expression.cpp)
target_include_directories(test_cron_expression PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_cron_expression PRIVATE Qt6::Core Qt6::Test supertinycron)
target_sources(test_cron_expression PRIVATE
    ${CMAKE_SOURCE_DIR}/src/CronExpression.cpp
    ${CMAKE_SOURCE_DIR}/src/CronExpression.h
)
add_test(NAME CronExpression COMMAND test_cron_expression)

# --- ScheduledTaskRegistry: add/update/remove roundtrip, persistence, enable/disable ---
qt_add_executable(test_scheduled_task_registry test_scheduled_task_registry.cpp)
target_include_directories(test_scheduled_task_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_scheduled_task_registry PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_scheduled_task_registry PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/ScheduledTaskDefinition.h
    ${CMAKE_SOURCE_DIR}/src/ScheduledTaskRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/ScheduledTaskRegistry.h
)
add_test(NAME ScheduledTaskRegistry COMMAND test_scheduled_task_registry)

# --- QuickFileOpenDialog scoring core: consecutive / word-boundary / camelCase
#     bonuses, pattern-longer-than-candidate, smart-case, and the basename-bonus
#     threshold guarantee (any basename match outranks any path-scattered match).
#     Pure static methods — no live dialog/git. QuickFileOpenDialog.cpp pulls in
#     widgets, hence Qt6::Gui/Widgets. ---
qt_add_executable(test_quick_file_open_scoring test_quick_file_open_scoring.cpp)
target_include_directories(test_quick_file_open_scoring PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_quick_file_open_scoring PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_quick_file_open_scoring PRIVATE
    ${CMAKE_SOURCE_DIR}/src/dialogs/QuickFileOpenDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/QuickFileOpenDialog.h
    ${CMAKE_SOURCE_DIR}/src/widgets/FuzzyHighlightDelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/FuzzyHighlightDelegate.h
    ${CMAKE_SOURCE_DIR}/src/FileIndexCache.h
)
add_test(NAME QuickFileOpenScoring COMMAND test_quick_file_open_scoring)

# --- QuickFileOpenDialog top-K search: bounded min-heap keeps exactly the
#     limit, tie-break stable & deterministic across runs, positions backtraced
#     only for survivors (exactly pattern-length each). ---
qt_add_executable(test_quick_file_open_topk test_quick_file_open_topk.cpp)
target_include_directories(test_quick_file_open_topk PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_quick_file_open_topk PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_quick_file_open_topk PRIVATE
    ${CMAKE_SOURCE_DIR}/src/dialogs/QuickFileOpenDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/QuickFileOpenDialog.h
    ${CMAKE_SOURCE_DIR}/src/widgets/FuzzyHighlightDelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/FuzzyHighlightDelegate.h
    ${CMAKE_SOURCE_DIR}/src/FileIndexCache.h
)
add_test(NAME QuickFileOpenTopK COMMAND test_quick_file_open_topk)

# --- QuickFileOpenDialog empty-query ordering: MRU-first (present in snapshot)
#     then enumeration order, MRU misses skipped, truncated to limit. ---
qt_add_executable(test_quick_file_open_empty test_quick_file_open_empty.cpp)
target_include_directories(test_quick_file_open_empty PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_quick_file_open_empty PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_quick_file_open_empty PRIVATE
    ${CMAKE_SOURCE_DIR}/src/dialogs/QuickFileOpenDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/QuickFileOpenDialog.h
    ${CMAKE_SOURCE_DIR}/src/widgets/FuzzyHighlightDelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/FuzzyHighlightDelegate.h
    ${CMAKE_SOURCE_DIR}/src/FileIndexCache.h
)
add_test(NAME QuickFileOpenEmpty COMMAND test_quick_file_open_empty)

# --- WorkspaceFileEnumerator merge + FileIndexCache arena: three-stream merge
#     dedupes and drops the untracked-submodule directory placeholder; arena
#     offsets bound each path; ASCII fold lowercases; non-ASCII folded on the
#     fly (not dropped). Enumerator uses QtConcurrent + GitProcessRunner, so
#     those sources/libs are linked even though the tested methods are pure. ---
qt_add_executable(test_workspace_enumerator_merge test_workspace_enumerator_merge.cpp)
target_include_directories(test_workspace_enumerator_merge PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_workspace_enumerator_merge PRIVATE Qt6::Core Qt6::Test Qt6::Concurrent)
target_sources(test_workspace_enumerator_merge PRIVATE
    ${CMAKE_SOURCE_DIR}/src/WorkspaceFileEnumerator.cpp
    ${CMAKE_SOURCE_DIR}/src/WorkspaceFileEnumerator.h
    ${CMAKE_SOURCE_DIR}/src/FileIndexCache.h
    ${CMAKE_SOURCE_DIR}/src/git/GitProcessRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitProcessRunner.h
)
add_test(NAME WorkspaceEnumeratorMerge COMMAND test_workspace_enumerator_merge)

# --- sanitizeDockArea: detached-dock-area guard for DockedEditor::currentDockArea().
#     Pure predicate, but it queries CDockAreaWidget::dockContainer(), so the ADS
#     library (qtadvanceddocking-qt6) is linked and the test constructs real
#     CDockAreaWidget/CDockManager widgets — hence QApplication via QTEST_MAIN and
#     the Qt6::Gui/Qt6::Widgets deps, matching the other widget-constructing tests. ---
qt_add_executable(test_docked_editor_current_area test_docked_editor_current_area.cpp)
target_include_directories(test_docked_editor_current_area PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_docked_editor_current_area PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test qtadvanceddocking-qt6)
target_sources(test_docked_editor_current_area PRIVATE
    ${CMAKE_SOURCE_DIR}/src/DockAreaSanitizer.cpp
    ${CMAKE_SOURCE_DIR}/src/DockAreaSanitizer.h
)
add_test(NAME DockedEditorCurrentArea COMMAND test_docked_editor_current_area)

# --- addDockWidgetAsBottomTool: VS Code-style split of a tool tab (terminal)
#     below the editor area inside the same ADS manager. Real CDockManager so
#     the Center-vs-Bottom placement is the actual ADS layout, not a stub. ---
qt_add_executable(test_dock_bottom_tool_tab test_dock_bottom_tool_tab.cpp)
target_include_directories(test_dock_bottom_tool_tab PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dock_bottom_tool_tab PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test qtadvanceddocking-qt6)
target_sources(test_dock_bottom_tool_tab PRIVATE
    ${CMAKE_SOURCE_DIR}/src/DockBottomToolTab.cpp
    ${CMAKE_SOURCE_DIR}/src/DockBottomToolTab.h
)
add_test(NAME DockBottomToolTab COMMAND test_dock_bottom_tool_tab)

# --- DockTabContextMenu: tab-order resolution for Qt's internal dock tab bar
#     plus the per-scope close-target decision (close / others / left / right /
#     all, with non-closable docks dropped). Real QMainWindow + QDockWidget are
#     constructed so the tab-order test reads the actual QMainWindowTabBar that
#     Qt builds when docks are tabified — hence Qt6::Widgets and QTEST_MAIN. ---
qt_add_executable(test_dock_tab_context_menu test_dock_tab_context_menu.cpp)
target_include_directories(test_dock_tab_context_menu PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_dock_tab_context_menu PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_dock_tab_context_menu PRIVATE
    ${CMAKE_SOURCE_DIR}/src/widgets/DockTabContextMenu.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/DockTabContextMenu.h
)
add_test(NAME DockTabContextMenu COMMAND test_dock_tab_context_menu)

# --- FolderAsWorkspaceProxyModel: synthetic-root proxy over the workspace
#     QFileSystemModel. Pure proxy unit test against a QTemporaryDir tree — no
#     live git. Covers QAbstractItemModelTester conformance, map round-trips
#     (invisible top / synthetic root / passthrough), structure (single top row,
#     parent(PR)==QModelIndex(), parent(child)==PR, columnCount==1), rowsInserted
#     /rowsRemoved forwarding (create + delete a file), and re-root / empty-root.
#     QFileSystemModel lives in Qt6::Gui; QAbstractItemModelTester in Qt6::Test.
#     The FsModel's data() override pulls in the git decoration palette + index,
#     so GitDiffPalette + PathStatusIndex sources are linked (not stubbed). ---
qt_add_executable(test_folder_workspace_proxy test_folder_workspace_proxy.cpp)
target_include_directories(test_folder_workspace_proxy PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_folder_workspace_proxy PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_sources(test_folder_workspace_proxy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/docks/FolderAsWorkspaceProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/FolderAsWorkspaceProxyModel.h
    ${CMAKE_SOURCE_DIR}/src/docks/FolderAsWorkspaceFsModel.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/FolderAsWorkspaceFsModel.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.h
    ${CMAKE_SOURCE_DIR}/src/git/PathStatusIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/git/PathStatusIndex.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusEntry.h
)
add_test(NAME FolderWorkspaceProxy COMMAND test_folder_workspace_proxy)

# --- RecursiveTreeWatcherLogic: pure decision logic for the selective-watch
#     working-tree watcher. isUnderIgnored() (per-event ignore filter incl.
#     exact-file ignore + separator guard) and deriveWatchedTopLevelDirs()
#     (which top-level dirs get a recursive watch, .git always excluded).
#     Header-only and Win32-free, so it compiles/runs on every platform — the
#     IOCP/handle lifecycle itself is not unit-testable in-tree. ---
qt_add_executable(test_recursive_tree_watcher_logic test_recursive_tree_watcher_logic.cpp)
target_include_directories(test_recursive_tree_watcher_logic PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_recursive_tree_watcher_logic PRIVATE Qt6::Core Qt6::Test)
add_test(NAME RecursiveTreeWatcherLogic COMMAND test_recursive_tree_watcher_logic)

# --- CsvTableModelInsert: structural-insert coordinate math for the CSV table
#     preview. Proves the virtual↔physical row/column translation, the overlay
#     re-key trap (an edited cell below/right of an insert keeps its value), the
#     dimension growth, the empty/header-only edge cases, and a save-walk
#     round-trip for a mid-file blank row. Pure model logic over a small mmap'd
#     temp file — no widget. ---
qt_add_executable(test_csv_table_model_insert test_csv_table_model_insert.cpp)
target_include_directories(test_csv_table_model_insert PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_csv_table_model_insert PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Core5Compat Qt6::Test uchardet)
target_sources(test_csv_table_model_insert PRIVATE
    ${CMAKE_SOURCE_DIR}/src/CsvTableModel.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvTableModel.h
    ${CMAKE_SOURCE_DIR}/src/CsvEditCommands.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvEditCommands.h
    ${CMAKE_SOURCE_DIR}/src/CsvDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvDocument.h
    ${CMAKE_SOURCE_DIR}/src/git/FileEncodingDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/git/FileEncodingDetector.h
)
add_test(NAME CsvTableModelInsert COMMAND test_csv_table_model_insert)

# --- CsvSortFilter: pure compute core for the CSV table preview's sort/filter
#     view layer. Proves numeric-column detection (incl. leading-zero=text),
#     stable numeric + lexicographic sort (asc/desc), empty/ragged sort-last,
#     single + multi-column ANDed substring filter, combined filter+sort in one
#     pass, identity order when no sort/filter, and the central safety property:
#     CsvTableModel::viewRowToDataRow() is the identity when no view is active.
#     CsvSortFilter is Qt-widget-free; the identity case links the model +
#     document over a small mmap'd temp file. ---
qt_add_executable(test_csv_sortfilter test_csv_sortfilter.cpp)
target_include_directories(test_csv_sortfilter PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
)
target_link_libraries(test_csv_sortfilter PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Core5Compat Qt6::Test uchardet)
target_sources(test_csv_sortfilter PRIVATE
    ${CMAKE_SOURCE_DIR}/src/CsvSortFilter.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvSortFilter.h
    ${CMAKE_SOURCE_DIR}/src/CsvTableModel.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvTableModel.h
    ${CMAKE_SOURCE_DIR}/src/CsvDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvDocument.h
    ${CMAKE_SOURCE_DIR}/src/git/FileEncodingDetector.cpp
    ${CMAKE_SOURCE_DIR}/src/git/FileEncodingDetector.h
)
add_test(NAME CsvSortFilter COMMAND test_csv_sortfilter)

# --- CsvFilterStrip: pure field-list reconciliation core for the CSV preview's
#     per-column filter strip. Locks down the position-aware splice (insert AT
#     index, not tail), the destroy+erase on remove (QPointer proves no widget
#     leak), and the count reconcile used by the reset/rebuild path. The
#     CsvPreviewWidget handlers delegate to these functions, so the test covers
#     the shipping path. Widget-free apart from QLineEdit, so it links only the
#     core .cpp + Qt Widgets (no document/model/uchardet). ---
qt_add_executable(test_csv_filter_strip test_csv_filter_strip.cpp)
target_include_directories(test_csv_filter_strip PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_csv_filter_strip PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_csv_filter_strip PRIVATE
    ${CMAKE_SOURCE_DIR}/src/CsvFilterStrip.cpp
    ${CMAKE_SOURCE_DIR}/src/CsvFilterStrip.h
)
add_test(NAME CsvFilterStrip COMMAND test_csv_filter_strip)

# --- SshProfileRegistry: profile persist/load round-trip + ssh:// URI parse/format ---
qt_add_executable(test_ssh_profile_registry test_ssh_profile_registry.cpp)
target_include_directories(test_ssh_profile_registry PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_ssh_profile_registry PRIVATE Qt6::Core Qt6::Test)
target_sources(test_ssh_profile_registry PRIVATE
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfileRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfileRegistry.h
)
add_test(NAME SshProfileRegistry COMMAND test_ssh_profile_registry)

# --- ExecutionContext resolver: local vs remote resolveCwd + the context-aware
#     TerminalCwdResolver overload (remote path NOT stat'd against local disk;
#     local behavior byte-for-byte unchanged). LocalExecutionContext pulls in
#     GitProcessRunner + libptyqt (createGitRunner/createPty seams), hence those
#     sources/libs even though the tested methods are pure. ---
qt_add_executable(test_execution_context_resolver test_execution_context_resolver.cpp)
target_include_directories(test_execution_context_resolver PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
    ${CMAKE_SOURCE_DIR}/thirdparty/libptyqt
)
target_link_libraries(test_execution_context_resolver PRIVATE
    Qt6::Core Qt6::Gui Qt6::Test libptyqt)
target_sources(test_execution_context_resolver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/TerminalCwdResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/TerminalCwdResolver.h
    ${CMAKE_SOURCE_DIR}/src/remote/ExecutionContext.h
    ${CMAKE_SOURCE_DIR}/src/remote/IFileSystemBackend.h
    ${CMAKE_SOURCE_DIR}/src/remote/LocalExecutionContext.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/LocalExecutionContext.h
    ${CMAKE_SOURCE_DIR}/src/git/GitProcessRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitProcessRunner.h
)
add_test(NAME ExecutionContextResolver COMMAND test_execution_context_resolver)

# --- CredentialStore keyed API: keyed store/retrieve/clear + the API-key
#     wrappers' byte-for-byte equivalence. Uses the real platform backend, so
#     skips gracefully when no usable OS keychain is present (headless CI).
#     QTEST_MAIN (QApplication) because the Windows backend reads settings via
#     a possible ApplicationSettings child. ---
qt_add_executable(test_credential_store_keyed test_credential_store_keyed.cpp)
target_include_directories(test_credential_store_keyed PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/ai
)
target_link_libraries(test_credential_store_keyed PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_credential_store_keyed PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Common.cpp
)
if(WIN32)
    target_sources(test_credential_store_keyed PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Windows.cpp)
    target_link_libraries(test_credential_store_keyed PRIVATE crypt32)
elseif(APPLE)
    target_sources(test_credential_store_keyed PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Mac.mm)
    target_link_libraries(test_credential_store_keyed PRIVATE
        "-framework Security" "-framework CoreFoundation")
else()
    target_sources(test_credential_store_keyed PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Linux.cpp)
endif()
add_test(NAME CredentialStoreKeyed COMMAND test_credential_store_keyed)

# --- SshSessionWorker (offline): the design's correctness-critical invariants
#     driven by FakeSshTransport + pumpForTest() — auth state machine, host-key
#     gate, FIFO channel queue at cap (D5), round-robin anti-starvation (D3),
#     write-EAGAIN no-spin / notifier-off-when-drained (D4), connection-loss
#     cleanup (D8). No network, no sshd — CI needs no SSH server. ---
qt_add_executable(test_ssh_session_worker test_ssh_session_worker.cpp)
target_include_directories(test_ssh_session_worker PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_ssh_session_worker PRIVATE Qt6::Core Qt6::Test)
target_sources(test_ssh_session_worker PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/FakeSshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/ISshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.h
)
add_test(NAME SshSessionWorker COMMAND test_ssh_session_worker)

# --- SshSessionWorker transport-core fixes (offline): the D0 FIX-1..FIX-4
#     correctness-critical invariants — ChannelBusy re-queue + backoff (FIX-1),
#     unified channel counter + budget + starvation reserve (FIX-2), keepalive
#     miss → connection-lost (FIX-3), non-blocking connect + cancel preempt
#     (FIX-4). Driven by FakeSshTransport + the *ForTest hooks — NO network, no
#     sshd, deterministic. ---
qt_add_executable(test_ssh_transport_fixes test_ssh_transport_fixes.cpp)
target_include_directories(test_ssh_transport_fixes PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_ssh_transport_fixes PRIVATE Qt6::Core Qt6::Test)
target_sources(test_ssh_transport_fixes PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/FakeSshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/ISshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.h
)
add_test(NAME SshTransportFixes COMMAND test_ssh_transport_fixes)

# --- RemoteFsBackend SFTP engine (offline): the D1 SFTP filesystem backend's
#     read/write/stat/readdir behavior + the one-channel-reuse invariant,
#     driven by FakeSshTransport + SshSessionWorker's SFTP slots. RemoteFsBackend
#     is a thin reqId→callback adapter over these exact worker slots/signals
#     (relayed 1:1 by SshConnection), so this drives its full data path without
#     the libssh2 / mbedTLS / CredentialStore stack a live SshConnection needs.
#     No network, no sshd — CI needs no SSH server. ---
qt_add_executable(test_remote_fs_backend test_remote_fs_backend.cpp)
target_include_directories(test_remote_fs_backend PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_remote_fs_backend PRIVATE Qt6::Core Qt6::Test)
target_sources(test_remote_fs_backend PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/FakeSshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/ISshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.h
)
add_test(NAME RemoteFsBackend COMMAND test_remote_fs_backend)

# --- RemoteFileSystemModel (offline): the D2 SFTP-backed workspace file-tree
#     model — the single highest-risk piece. Driven by an in-memory FAKE lister
#     (a path → entries map), so NO network / SshConnection / libssh2 is linked:
#     the model takes a ListFn seam expressly for this. Asserts
#     QAbstractItemModelTester conformance, index(path)/filePath/isDir round-trips
#     across the synthetic-root regions, rowsInserted on fetchMore, directoryLoaded
#     per listed dir, onDirectoryChanged insert/remove diff, empty-dir settle, and
#     a FolderAsWorkspaceProxyModel round-trip proving the proxy's synthetic-root
#     invariants hold VERBATIM over the remote model. RemoteFileSystemModel owns a
#     self-contained POD Entry type, so it pulls in NO SSH headers/sources at all.
#     QAbstractItemModelTester queries DecorationRole (a QIcon), so a GUI
#     application context (Qt6::Gui via QTEST_MAIN) is required. ---
qt_add_executable(test_remote_filesystem_model test_remote_filesystem_model.cpp)
target_include_directories(test_remote_filesystem_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_SOURCE_DIR}/src/docks
)
target_link_libraries(test_remote_filesystem_model PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_sources(test_remote_filesystem_model PRIVATE
    ${CMAKE_SOURCE_DIR}/src/remote/IWorkspaceFsModel.h
    ${CMAKE_SOURCE_DIR}/src/remote/RemoteFileSystemModel.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/RemoteFileSystemModel.h
    ${CMAKE_SOURCE_DIR}/src/docks/FolderAsWorkspaceProxyModel.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/FolderAsWorkspaceProxyModel.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.h
    ${CMAKE_SOURCE_DIR}/src/git/PathStatusIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/git/PathStatusIndex.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusEntry.h
)
add_test(NAME RemoteFilesystemModel COMMAND test_remote_filesystem_model)

# --- RemoteDirectoryWatcher (offline): the D3 bounded remote poll-watcher
#     (tasks 3.1+3.2). Driven by a FAKE in-memory poller (a path → entries map)
#     + a scriptable visible-dir set, so NO network / SshConnection /
#     RemoteFsBackend / libssh2 is linked: the watcher takes a PollFn +
#     VisibleDirsFn seam expressly for this. Asserts the normative invariants:
#       - first sighting seeds the baseline silently (no spurious change)
#       - add / remove / metadata-change detection → directoryChanged(path)
#       - unchanged listing emits nothing
#       - ONLY visible/expanded dirs polled (deep tree, one expanded dir →
#         exactly that dir; non-recursive), collapse prunes the cache
#       - interval gating: 2s focused+visible / 10s unfocused / paused on
#         minimize OR dock-hidden, via currentIntervalMsForTest + the timer state
#       - transient readdir failure keeps the cached baseline
#       - in-flight guard avoids request pile-up on a slow link
#     Drives poll-ticks directly via pollNowForTest() — no 2s event-loop wait.
#     Pure Qt6::Core; QTEST_MAIN provides the QCoreApplication the QTimer needs. ---
qt_add_executable(test_remote_poll_watch test_remote_poll_watch.cpp)
target_include_directories(test_remote_poll_watch PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
)
target_link_libraries(test_remote_poll_watch PRIVATE Qt6::Core Qt6::Test)
target_sources(test_remote_poll_watch PRIVATE
    ${CMAKE_SOURCE_DIR}/src/remote/RemoteDirectoryWatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/RemoteDirectoryWatcher.h
)
add_test(NAME RemotePollWatch COMMAND test_remote_poll_watch)

# --- RemoteGitProcessRunner / exec engine (offline): the D6 git-over-SSH path.
#     Two layers, both on FakeSshTransport: (1) the runner's pure argv → exec
#     command-string construction + POSIX single-quoting; (2) the SshSessionWorker
#     exec engine (requestExec + serviceExecForTest + exec*Chunk/execDone) which
#     is the worker half of RemoteGitProcessRunner — stdout/stderr capture, exit
#     mapping, stdin feed, EAGAIN resume, cancel. The runner is a thin
#     reqId→callback adapter over these slots/signals (relayed 1:1 by
#     SshConnection), so this drives its full data path without the libssh2 /
#     mbedTLS / CredentialStore stack a live SshConnection needs. No network,
#     no sshd — CI needs no SSH server. ---
qt_add_executable(test_remote_git_runner test_remote_git_runner.cpp)
target_include_directories(test_remote_git_runner PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_remote_git_runner PRIVATE Qt6::Core Qt6::Test)
target_sources(test_remote_git_runner PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/FakeSshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/ISshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.h
)
add_test(NAME RemoteGitRunner COMMAND test_remote_git_runner)

# --- ACP over SSH (offline): the D8 agent-over-SSH path. Three layers, all on
#     FakeSshTransport: (1) SshAcpProcessChannel's pure `cd <cwd> && exec ...`
#     command-string construction + POSIX quoting; (2) the exec-channel IO adapter
#     end-to-end — write(frame) → channel stdin (chWritten), scripted stdout →
#     readyReadStdout, channel exit → finished — driven through a real
#     SshSessionWorker exec engine (incl. the D8 streamed-stdin requestExecWrite
#     path) wrapped in a WorkerExecHost; (3) AcpConnection's remote binary probe:
#     a `command -v` MISS emits a clear error and spawns nothing (no local
#     fallback), a HIT consults the injected channel builder and starts it.
#     AcpConnection is exercised against a fake remote ExecutionContext, so no
#     libssh2 / mbedTLS / CredentialStore / live SshConnection. No network, no
#     sshd — CI needs no SSH server. ---
qt_add_executable(test_acp_remote_transport test_acp_remote_transport.cpp)
target_include_directories(test_acp_remote_transport PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
    ${CMAKE_CURRENT_SOURCE_DIR}
)
target_link_libraries(test_acp_remote_transport PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_sources(test_acp_remote_transport PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/FakeSshTransport.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/IAcpExecHost.h
    ${CMAKE_SOURCE_DIR}/src/IAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/SshAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/SshAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/remote/ExecutionContext.h
    ${CMAKE_SOURCE_DIR}/src/remote/ISshTransport.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshSessionWorker.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/SshHostKeyStore.h
)
add_test(NAME AcpRemoteTransport COMMAND test_acp_remote_transport)

# --- Async editor open (offline): the D4 shell-then-load path. Drives
#     ScintillaNext::createShell + loadInto over a FAKE remote IFileSystemBackend
#     (no SSH / SshConnection / libssh2): createShell returns a File + ssh://-URI
#     identity editor IMMEDIATELY (so DockedEditor::initialEditor() would reject
#     it — never reused as a scratch tab); loadInto fills the buffer from the fake
#     backend's deferred callback; opening a second file mid-load leaves the
#     loading tab untouched; URI is the dedupe identity; read failure → Error +
#     Retry. Compiles ScintillaNext.cpp + its small helper sources against the
#     vendored scintilla lib + uchardet (the editor's encoding dep). ---
qt_add_executable(test_async_editor_load test_async_editor_load.cpp)
target_include_directories(test_async_editor_load PRIVATE
    ${CMAKE_SOURCE_DIR}/src
)
target_link_libraries(test_async_editor_load PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test scintilla uchardet)
target_sources(test_async_editor_load PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ScintillaNext.cpp
    ${CMAKE_SOURCE_DIR}/src/ScintillaNext.h
    ${CMAKE_SOURCE_DIR}/src/RangeAllocator.cpp
    ${CMAKE_SOURCE_DIR}/src/RangeAllocator.h
    ${CMAKE_SOURCE_DIR}/src/ScintillaCommenter.cpp
    ${CMAKE_SOURCE_DIR}/src/ScintillaCommenter.h
    ${CMAKE_SOURCE_DIR}/src/SelectionTracker.cpp
    ${CMAKE_SOURCE_DIR}/src/SelectionTracker.h
    ${CMAKE_SOURCE_DIR}/src/UndoAction.cpp
    ${CMAKE_SOURCE_DIR}/src/UndoAction.h
    ${CMAKE_SOURCE_DIR}/src/ByteArrayUtils.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
    ${CMAKE_SOURCE_DIR}/src/remote/IFileSystemBackend.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
    ${CMAKE_SOURCE_DIR}/src/QRegexSearch.cpp
    ${CMAKE_SOURCE_DIR}/src/QRegexSearch.h
)
add_test(NAME AsyncEditorLoad COMMAND test_async_editor_load)

# --- Embedded-window lifecycle (offline): pure state-machine + injected fake
#     platform hooks. No HWND, desktop, ADS, or NativeWindowHost dependency. ---
qt_add_executable(test_embedded_window_controller test_embedded_window_controller.cpp)
target_include_directories(test_embedded_window_controller PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_embedded_window_controller PRIVATE Qt6::Core Qt6::Test)
target_sources(test_embedded_window_controller PRIVATE
    ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowController.cpp
    ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowController.h
    ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowWin32.h
    ${CMAKE_SOURCE_DIR}/src/PendingCloseState.h
)
add_test(NAME EmbeddedWindowController COMMAND test_embedded_window_controller)

# --- Embedded-window focus routing (offline): coalescing, tab identity, and
#     QObject lifetime guards. No HWND, ADS, or foreign process required. ---
qt_add_executable(test_embedded_window_focus_router test_embedded_window_focus_router.cpp)
target_include_directories(test_embedded_window_focus_router PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_embedded_window_focus_router PRIVATE Qt6::Widgets Qt6::Test)
target_sources(test_embedded_window_focus_router PRIVATE
    ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowFocusRouter.h
)
add_test(NAME EmbeddedWindowFocusRouter COMMAND test_embedded_window_focus_router)

# --- Embedded-window live behaviour (Windows, interactive desktop): embeds a
#     real foreign process (embed_test_target) and drives it with SendInput.
#     Skips unless NOTEPADAI_INTERACTIVE_TESTS=1 because it takes the foreground. ---
if(WIN32)
    add_executable(embed_test_target WIN32 embed_test_target.cpp embed_test_target_shared.h)
    target_link_libraries(embed_test_target PRIVATE shell32 user32 gdi32)
    qt_add_executable(test_embedded_window_win32 test_embedded_window_win32.cpp)
    target_include_directories(test_embedded_window_win32 PRIVATE ${CMAKE_SOURCE_DIR}/src)
    target_link_libraries(test_embedded_window_win32 PRIVATE Qt6::Widgets Qt6::Test dwmapi comctl32)
    target_sources(test_embedded_window_win32 PRIVATE
        ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowFocusRouter.h
        ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowWin32.cpp
        ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowWin32.h
        ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowEnumeration.cpp
        ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowWorker.cpp
        ${CMAKE_SOURCE_DIR}/src/EmbeddedWindowWorker.h
        ${CMAKE_SOURCE_DIR}/src/NativeWindowHost.cpp
        ${CMAKE_SOURCE_DIR}/src/NativeWindowHost.h
        embed_test_target_shared.h
    )
    target_compile_definitions(test_embedded_window_win32 PRIVATE
        EMBED_TEST_TARGET_PATH="$<TARGET_FILE:embed_test_target>"
        EMBED_BLENDER_SCRIPT_PATH="${CMAKE_CURRENT_SOURCE_DIR}/embed_blender_log_events.py")
    add_dependencies(test_embedded_window_win32 embed_test_target)
    add_test(NAME EmbeddedWindowWin32 COMMAND test_embedded_window_win32)
endif()

# --- LlmHttpClient: OpenAI vs Anthropic payload / URL encoding ---
qt_add_executable(test_llm_http_client test_llm_http_client.cpp)
target_include_directories(test_llm_http_client PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_llm_http_client PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_sources(test_llm_http_client PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ai/LlmHttpClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/LlmHttpClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/ILlmHttpClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/SsePartialParser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/SsePartialParser.h
)
add_test(NAME LlmHttpClient COMMAND test_llm_http_client)

# --- CommitMessageTool: submit_commit_message schema + argument validation ---
qt_add_executable(test_commit_message_tool test_commit_message_tool.cpp)
target_include_directories(test_commit_message_tool PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_commit_message_tool PRIVATE Qt6::Core Qt6::Test)
target_sources(test_commit_message_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ai/CommitMessageTool.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/CommitMessageTool.h
    ${CMAKE_SOURCE_DIR}/src/ai/ILlmHttpClient.h
)
add_test(NAME CommitMessageTool COMMAND test_commit_message_tool)

# --- OpenaiAnthropicBridge: Mini App page-agent chat-completions ↔ /v1/messages ---
qt_add_executable(test_openai_anthropic_bridge test_openai_anthropic_bridge.cpp)
target_include_directories(test_openai_anthropic_bridge PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_openai_anthropic_bridge PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_sources(test_openai_anthropic_bridge PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ai/OpenaiAnthropicBridge.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/OpenaiAnthropicBridge.h
    ${CMAKE_SOURCE_DIR}/src/ai/LlmHttpClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/LlmHttpClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/ILlmHttpClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/SsePartialParser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/SsePartialParser.h
)
add_test(NAME OpenaiAnthropicBridge COMMAND test_openai_anthropic_bridge)

# --- SsePartialParser: OpenAI chat-completions + Anthropic /v1/messages SSE ---
qt_add_executable(test_sse_partial_parser test_sse_partial_parser.cpp)
target_include_directories(test_sse_partial_parser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_sse_partial_parser PRIVATE Qt6::Core Qt6::Test)
target_sources(test_sse_partial_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ai/SsePartialParser.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/SsePartialParser.h
)
add_test(NAME SsePartialParser COMMAND test_sse_partial_parser)

# --- GoalHttpJudge: Anthropic /v1/messages URL, tool-verdict parse, weak-LLM retry ---
qt_add_executable(test_goal_http_judge test_goal_http_judge.cpp)
target_include_directories(test_goal_http_judge PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_goal_http_judge PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
target_sources(test_goal_http_judge PRIVATE
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.h
    ${CMAKE_SOURCE_DIR}/src/ai/IAnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Common.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
)
add_test(NAME GoalHttpJudge COMMAND test_goal_http_judge)
if(WIN32)
    target_sources(test_goal_http_judge PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Windows.cpp)
    target_link_libraries(test_goal_http_judge PRIVATE crypt32)
elseif(APPLE)
    target_sources(test_goal_http_judge PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Mac.mm)
    target_link_libraries(test_goal_http_judge PRIVATE
        "-framework Security" "-framework CoreFoundation")
else()
    target_sources(test_goal_http_judge PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Linux.cpp)
endif()

# --- GoalAgent: Stop tears down the judge, not the target ACP prompt ---
qt_add_executable(test_goal_agent test_goal_agent.cpp)
target_include_directories(test_goal_agent PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
)
target_link_libraries(test_goal_agent PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Test)
target_sources(test_goal_agent PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpPromptQueue.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpPromptQueue.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalVerdictMcp.cpp
    ${CMAKE_SOURCE_DIR}/src/McpStdio.cpp
    ${CMAKE_SOURCE_DIR}/src/McpStdio.h
    ${CMAKE_SOURCE_DIR}/src/GoalVerdictMcp.h
    ${CMAKE_SOURCE_DIR}/src/PiAcpJudgeConfig.cpp
    ${CMAKE_SOURCE_DIR}/src/PiAcpJudgeConfig.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalConversationSummary.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudge.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeRunner.h
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalHttpJudgeSession.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
    ${CMAKE_SOURCE_DIR}/src/IAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/ai/IAnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.cpp
    ${CMAKE_SOURCE_DIR}/src/ai/AnthropicMessagesClient.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore.h
    ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Common.cpp
)
add_test(NAME GoalAgent COMMAND test_goal_agent)
if(WIN32)
    target_sources(test_goal_agent PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Windows.cpp)
    target_link_libraries(test_goal_agent PRIVATE crypt32)
elseif(APPLE)
    target_sources(test_goal_agent PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Mac.mm)
    target_link_libraries(test_goal_agent PRIVATE
        "-framework Security" "-framework CoreFoundation")
else()
    target_sources(test_goal_agent PRIVATE
        ${CMAKE_SOURCE_DIR}/src/ai/CredentialStore_Linux.cpp)
endif()

# --- ZipPath: zip-slip validation, wrapper-prefix strip, reserved names ---
qt_add_executable(test_zip_path test_zip_path.cpp)
target_include_directories(test_zip_path PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_zip_path PRIVATE Qt6::Core Qt6::Test)
target_sources(test_zip_path PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ZipPath.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipPath.h
)
add_test(NAME ZipPath COMMAND test_zip_path)

# --- ZipArchive: miniz pack/extract round-trip ---
qt_add_executable(test_zip_archive test_zip_archive.cpp)
target_include_directories(test_zip_archive PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_zip_archive PRIVATE Qt6::Core Qt6::Test miniz)
target_sources(test_zip_archive PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ZipArchive.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipArchive.h
)
add_test(NAME ZipArchive COMMAND test_zip_archive)

# --- ZipIgnoreWalk: .git / node_modules skip + gitignore ---
qt_add_executable(test_zip_ignore_walk test_zip_ignore_walk.cpp)
target_include_directories(test_zip_ignore_walk PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
)
target_link_libraries(test_zip_ignore_walk PRIVATE Qt6::Core Qt6::Test wildmatch_c)
target_sources(test_zip_ignore_walk PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ZipIgnoreWalk.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipIgnoreWalk.h
    ${CMAKE_SOURCE_DIR}/src/ZipPath.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipPath.h
    ${CMAKE_SOURCE_DIR}/src/remote/GitignoreMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/GitignoreMatcher.h
)
add_test(NAME ZipIgnoreWalk COMMAND test_zip_ignore_walk)

# --- ZipUploadWipe: post-extract orphan set ---
qt_add_executable(test_zip_upload_wipe test_zip_upload_wipe.cpp)
target_include_directories(test_zip_upload_wipe PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
)
target_link_libraries(test_zip_upload_wipe PRIVATE Qt6::Core Qt6::Test wildmatch_c miniz)
target_sources(test_zip_upload_wipe PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ZipUploadWipe.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipUploadWipe.h
    ${CMAKE_SOURCE_DIR}/src/ZipIgnoreWalk.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipIgnoreWalk.h
    ${CMAKE_SOURCE_DIR}/src/ZipPath.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipPath.h
    ${CMAKE_SOURCE_DIR}/src/ZipArchive.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipArchive.h
    ${CMAKE_SOURCE_DIR}/src/remote/GitignoreMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/GitignoreMatcher.h
)
add_test(NAME ZipUploadWipe COMMAND test_zip_upload_wipe)

# --- CrocTool: code-phrase parse + release asset hint ---
qt_add_executable(test_croc_tool test_croc_tool.cpp)
target_include_directories(test_croc_tool PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_croc_tool PRIVATE Qt6::Core Qt6::Network Qt6::Test miniz)
target_sources(test_croc_tool PRIVATE
    ${CMAKE_SOURCE_DIR}/src/CrocTool.cpp
    ${CMAKE_SOURCE_DIR}/src/CrocTool.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
    ${CMAKE_SOURCE_DIR}/src/ZipArchive.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipArchive.h
)
add_test(NAME CrocTool COMMAND test_croc_tool)

# --- zip_bench: manual end-to-end walk+pack timing (no ctest, large tree) ---
qt_add_executable(zip_bench zip_bench.cpp)
target_include_directories(zip_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/remote
)
target_link_libraries(zip_bench PRIVATE Qt6::Core miniz wildmatch_c)
target_sources(zip_bench PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ZipArchive.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipArchive.h
    ${CMAKE_SOURCE_DIR}/src/ZipIgnoreWalk.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipIgnoreWalk.h
    ${CMAKE_SOURCE_DIR}/src/ZipPath.cpp
    ${CMAKE_SOURCE_DIR}/src/ZipPath.h
    ${CMAKE_SOURCE_DIR}/src/remote/GitignoreMatcher.cpp
    ${CMAKE_SOURCE_DIR}/src/remote/GitignoreMatcher.h
)

# --- Optional: real-localhost-sshd smoke test of Libssh2Transport end-to-end.
#     Excluded from CI (needs a live sshd + libssh2 vendored). Enable with
#     -DNOTEPADAI_SSH_INTEGRATION=ON and the env NOTEPADAI_SSH_TEST_HOST/USER. ---
option(NOTEPADAI_SSH_INTEGRATION "Build the localhost-sshd SSH integration smoke test" OFF)
if(NOTEPADAI_SSH_INTEGRATION AND EXISTS ${CMAKE_SOURCE_DIR}/tests/test_ssh_integration.cpp)
    qt_add_executable(test_ssh_integration test_ssh_integration.cpp)
    target_include_directories(test_ssh_integration PRIVATE
        ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/remote)
    target_link_libraries(test_ssh_integration PRIVATE Qt6::Core Qt6::Test libssh2)
    target_sources(test_ssh_integration PRIVATE
        ${CMAKE_SOURCE_DIR}/src/remote/Libssh2Transport.cpp
        ${CMAKE_SOURCE_DIR}/src/remote/Libssh2Transport.h
    )
    if(WIN32)
        target_link_libraries(test_ssh_integration PRIVATE ws2_32)
    endif()
    add_test(NAME SshIntegration COMMAND test_ssh_integration)
endif()

# --- DebugLogDock: qWarning from a non-GUI thread must not touch the log
#     widget until the GUI thread processes events. Delete Permanently on a
#     watched folder makes QFileSystemWatcher's engine thread qErrnoWarning;
#     a synchronous appendPlainText from that thread crashes in HarfBuzz. ---
qt_add_executable(test_debug_log_dock test_debug_log_dock.cpp)
target_include_directories(test_debug_log_dock PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/docks
)
target_link_libraries(test_debug_log_dock PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_debug_log_dock PRIVATE
    ${CMAKE_SOURCE_DIR}/src/docks/DebugLogDock.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/DebugLogDock.h
    ${CMAKE_SOURCE_DIR}/src/docks/DebugLogDock.ui
    ${CMAKE_SOURCE_DIR}/src/DebugManager.cpp
    ${CMAKE_SOURCE_DIR}/src/DebugManager.h
)
add_test(NAME DebugLogDock COMMAND test_debug_log_dock)

# --- WatchedPathDelete: deleting a directory the file model is watching must
#     not leave the watch armed. Qt's watcher thread otherwise qErrnoWarning
#     "FindNextChangeNotification failed … Access is denied" (and the watch
#     handle can lock the delete, QTBUG-65683). ---
qt_add_executable(test_watched_path_delete test_watched_path_delete.cpp)
target_include_directories(test_watched_path_delete PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_watched_path_delete PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_watched_path_delete PRIVATE
    ${CMAKE_SOURCE_DIR}/src/WatchedPathDelete.cpp
    ${CMAKE_SOURCE_DIR}/src/WatchedPathDelete.h
)
add_test(NAME WatchedPathDelete COMMAND test_watched_path_delete)

# --- ProjectGoalPresets: one file per criterion under .agents/.goals ---
qt_add_executable(test_project_goal_presets test_project_goal_presets.cpp)
target_include_directories(test_project_goal_presets PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_project_goal_presets PRIVATE Qt6::Core Qt6::Test)
target_sources(test_project_goal_presets PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.cpp
    ${CMAKE_SOURCE_DIR}/src/ProjectGoalPresets.h
)
add_test(NAME ProjectGoalPresets COMMAND test_project_goal_presets)

# GoalAgentSettings.cpp calls Q_INIT_RESOURCE(prompts). Every target that
# compiles that file must also compile the qrc, or the link fails.
foreach(goal_prompt_test
        test_goal_draft_generator
        test_send_with_goal_dialog
        test_acp_agent_manager
        test_acp_agent_exit_restart
        test_acp_dock_close_event
        test_acp_session_view_wake
        test_goal_http_judge
        test_goal_agent)
    target_sources(${goal_prompt_test} PRIVATE ${CMAKE_SOURCE_DIR}/src/prompts/prompts.qrc)
endforeach()
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/src/prompts/goal-agent.md" GOAL_AGENT_PROMPT_FILE)
target_compile_definitions(test_goal_agent PRIVATE
    "GOAL_AGENT_PROMPT_FILE=\"${GOAL_AGENT_PROMPT_FILE}\"")

# --- AcpTurnFolding: "Worked for Xs" fold plan for finished turns ---
qt_add_executable(test_acp_turn_folding test_acp_turn_folding.cpp)
target_include_directories(test_acp_turn_folding PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_turn_folding PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_turn_folding PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTranscriptTruncation.h
    ${CMAKE_SOURCE_DIR}/src/AcpTurnFolding.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpTurnFolding.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
)
add_test(NAME AcpTurnFolding COMMAND test_acp_turn_folding)

# --- AcpDiffStat: +N -M line counts for the "Worked for" fold row ---
qt_add_executable(test_acp_diff_stat test_acp_diff_stat.cpp)
target_include_directories(test_acp_diff_stat PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_diff_stat PRIVATE Qt6::Core Qt6::Test xdiff)
target_sources(test_acp_diff_stat PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpDiffStat.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpDiffStat.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/git/BufferDiffEngine.cpp
    ${CMAKE_SOURCE_DIR}/src/git/BufferDiffEngine.h
)
add_test(NAME AcpDiffStat COMMAND test_acp_diff_stat)

# --- AcpConversationCopy: final-report detection + "Copy conversation" markdown ---
qt_add_executable(test_acp_conversation_copy test_acp_conversation_copy.cpp)
target_include_directories(test_acp_conversation_copy PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_conversation_copy PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_conversation_copy PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpConversationCopy.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConversationCopy.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
)
add_test(NAME AcpConversationCopy COMMAND test_acp_conversation_copy)

# --- MarkdownPreprocessor: math delimiters + allowlisted HTML -> markdown ---
qt_add_executable(test_markdown_preprocessor test_markdown_preprocessor.cpp)
target_include_directories(test_markdown_preprocessor PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_markdown_preprocessor PRIVATE Qt6::Core Qt6::Test)
target_sources(test_markdown_preprocessor PRIVATE
    ${CMAKE_SOURCE_DIR}/src/markdown/MarkdownPreprocessor.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MarkdownPreprocessor.h
)
add_test(NAME MarkdownPreprocessor COMMAND test_markdown_preprocessor)

# --- MathParser: TeX math subset -> AST ---
qt_add_executable(test_math_parser test_math_parser.cpp)
target_include_directories(test_math_parser PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_math_parser PRIVATE Qt6::Core Qt6::Test)
target_sources(test_math_parser PRIVATE
    ${CMAKE_SOURCE_DIR}/src/markdown/MathParser.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathParser.h
)
add_test(NAME MathParser COMMAND test_math_parser)

# --- MathLayout: TeX-style layout + QPainter rendering (needs a GUI app; offscreen) ---
qt_add_executable(test_math_layout test_math_layout.cpp)
target_include_directories(test_math_layout PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_math_layout PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_sources(test_math_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/src/markdown/MathLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathLayout.h
    ${CMAKE_SOURCE_DIR}/src/markdown/MathParser.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathParser.h
)
file(TO_CMAKE_PATH "${CMAKE_SOURCE_DIR}/src/fonts/STIXTwoMath-Regular.ttf" MATH_FONT_FILE_PATH)
target_compile_definitions(test_math_layout PRIVATE "MATH_FONT_FILE=\"${MATH_FONT_FILE_PATH}\"")
add_test(NAME MathLayout COMMAND test_math_layout)
set_tests_properties(MathLayout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- MathRenderCache: LRU of rendered formulas ---
qt_add_executable(test_math_render_cache test_math_render_cache.cpp)
target_include_directories(test_math_render_cache PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_math_render_cache PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
target_sources(test_math_render_cache PRIVATE
    ${CMAKE_SOURCE_DIR}/src/markdown/MathRenderCache.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathRenderCache.h
    ${CMAKE_SOURCE_DIR}/src/markdown/MathLayout.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathLayout.h
    ${CMAKE_SOURCE_DIR}/src/markdown/MathParser.cpp
    ${CMAKE_SOURCE_DIR}/src/markdown/MathParser.h
)
target_compile_definitions(test_math_render_cache PRIVATE "MATH_FONT_FILE=\"${MATH_FONT_FILE_PATH}\"")
add_test(NAME MathRenderCache COMMAND test_math_render_cache)
set_tests_properties(MathRenderCache PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- AcpMessageWidget markdown: math images, <details> toggle, align, link routing ---
qt_add_executable(test_acp_message_widget_markdown test_acp_message_widget_markdown.cpp)
target_include_directories(test_acp_message_widget_markdown PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_message_widget_markdown PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_message_widget_markdown PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
    ${MARKDOWN_RENDER_SOURCES}
)
add_test(NAME AcpMessageWidgetMarkdown COMMAND test_acp_message_widget_markdown)

# --- AcpMessageWidget user bubble: right-aligned layout in a narrow transcript ---
qt_add_executable(test_acp_user_bubble_layout test_acp_user_bubble_layout.cpp)
target_include_directories(test_acp_user_bubble_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_user_bubble_layout PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_user_bubble_layout PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
    ${MARKDOWN_RENDER_SOURCES}
)
add_test(NAME AcpUserBubbleLayout COMMAND test_acp_user_bubble_layout)
set_tests_properties(AcpUserBubbleLayout PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- MarkdownView: the shared markdown renderer (chat + file preview), width cap ---
qt_add_executable(test_markdown_view test_markdown_view.cpp)
target_include_directories(test_markdown_view PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_markdown_view PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_markdown_view PRIVATE ${MARKDOWN_RENDER_SOURCES})
add_test(NAME MarkdownView COMMAND test_markdown_view)

# --- MarkdownPreviewSupport: source clamp + scroll-sync mapping (pure) ---
qt_add_executable(test_markdown_preview_support test_markdown_preview_support.cpp)
target_include_directories(test_markdown_preview_support PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_markdown_preview_support PRIVATE Qt6::Core Qt6::Test)
add_test(NAME MarkdownPreviewSupport COMMAND test_markdown_preview_support)

# --- GrillMeForm: tool input parsing, answer formatting, question-round hint ---
qt_add_executable(test_grill_me_form test_grill_me_form.cpp)
target_include_directories(test_grill_me_form PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_grill_me_form PRIVATE Qt6::Core Qt6::Test)
target_sources(test_grill_me_form PRIVATE
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
)
add_test(NAME GrillMeForm COMMAND test_grill_me_form)

# --- GrillMeFormWidget: layout in a narrow transcript (long agent text) ---
qt_add_executable(test_grill_me_form_widget test_grill_me_form_widget.cpp)
target_include_directories(test_grill_me_form_widget PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_grill_me_form_widget PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_grill_me_form_widget PRIVATE
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/widgets/GrillMeFormWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/GrillMeFormWidget.h
)
add_test(NAME GrillMeFormWidget COMMAND test_grill_me_form_widget)
set_tests_properties(GrillMeFormWidget PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# --- GrillMeMcp: grill_me MCP server (C++ and the remote Node script) ---
qt_add_executable(test_grill_me_mcp test_grill_me_mcp.cpp)
target_include_directories(test_grill_me_mcp PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_grill_me_mcp PRIVATE Qt6::Core Qt6::Test)
target_sources(test_grill_me_mcp PRIVATE
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/GrillMeMcp.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeMcp.h
    ${CMAKE_SOURCE_DIR}/src/McpStdio.cpp
    ${CMAKE_SOURCE_DIR}/src/McpStdio.h
)
add_test(NAME GrillMeMcp COMMAND test_grill_me_mcp)

# --- AcpConnection: no form elicitation, grill_me auto-allow ---
qt_add_executable(test_acp_grill_me_permission test_acp_grill_me_permission.cpp)
target_include_directories(test_acp_grill_me_permission PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_grill_me_permission PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_grill_me_permission PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpScheduledJobs.h
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.cpp
    ${CMAKE_SOURCE_DIR}/src/GrillMeForm.h
    ${CMAKE_SOURCE_DIR}/src/IAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.h
)
add_test(NAME AcpGrillMePermission COMMAND test_acp_grill_me_permission)

# --- FileKindResolver: the one open-routing decision (extension, magic, NUL) ---
qt_add_executable(test_file_kind_resolver test_file_kind_resolver.cpp)
target_include_directories(test_file_kind_resolver PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_file_kind_resolver PRIVATE Qt6::Core Qt6::Test)
target_sources(test_file_kind_resolver PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preview/FileKindResolver.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/FileKindResolver.h
)
add_test(NAME FileKindResolver COMMAND test_file_kind_resolver)

# --- ImageViewMath + ImageOps: image canvas zoom/pan math, pyramid, crops ---
qt_add_executable(test_image_view_math test_image_view_math.cpp)
target_include_directories(test_image_view_math PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_image_view_math PRIVATE Qt6::Core Qt6::Gui Qt6::Concurrent Qt6::Test)
target_sources(test_image_view_math PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preview/ImageDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/ImageDocument.h
    ${CMAKE_SOURCE_DIR}/src/preview/ImageViewMath.h
)
add_test(NAME ImageViewMath COMMAND test_image_view_math)

# --- Texture decoders: DDS (legacy/DX10, BC, float), TGA via stb, Aseprite ---
qt_add_executable(test_texture_decoders test_texture_decoders.cpp)
target_include_directories(test_texture_decoders PRIVATE ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/preview)
target_link_libraries(test_texture_decoders PRIVATE
    Qt6::Core Qt6::Gui Qt6::Svg Qt6::Concurrent Qt6::Test stb_image bcdec tinyexr miniz)
target_sources(test_texture_decoders PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preview/ImageDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/ImageDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/DdsDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/RasterDecoders.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/AsepriteDecoder.cpp
)
add_test(NAME TextureDecoders COMMAND test_texture_decoders)

# --- Model loaders: STL/PLY/VOX/glTF/GLB/OBJ, camera framing, animation sampling ---
qt_add_executable(test_model_loaders test_model_loaders.cpp)
target_include_directories(test_model_loaders PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/preview ${CMAKE_SOURCE_DIR}/src/preview/model)
target_link_libraries(test_model_loaders PRIVATE
    Qt6::Core Qt6::Gui Qt6::Svg Qt6::Concurrent Qt6::Test stb_image bcdec tinyexr miniz cgltf ufbx fast_obj)
target_sources(test_model_loaders PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preview/ImageDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/ImageDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/DdsDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/RasterDecoders.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/AsepriteDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/model/ModelDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/model/ModelLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/model/GltfLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/model/FbxLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/model/ObjLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/model/SimpleMeshLoaders.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/model/ModelAnimator.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/model/ModelCamera.cpp
)
add_test(NAME ModelLoaders COMMAND test_model_loaders)

# --- 2D game formats: Tiled / LDtk tile maps, texture atlases, BMFont ---
qt_add_executable(test_2d_formats test_2d_formats.cpp)
target_include_directories(test_2d_formats PRIVATE
    ${CMAKE_SOURCE_DIR}/src ${CMAKE_SOURCE_DIR}/src/preview ${CMAKE_SOURCE_DIR}/src/preview/tilemap
    ${CMAKE_SOURCE_DIR}/src/preview/atlas)
target_link_libraries(test_2d_formats PRIVATE
    Qt6::Core Qt6::Gui Qt6::Svg Qt6::Concurrent Qt6::Test stb_image bcdec tinyexr miniz)
target_sources(test_2d_formats PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preview/ImageDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/ImageDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/DdsDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/RasterDecoders.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/AsepriteDecoder.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/tilemap/TileMapDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/tilemap/TileMapLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/tilemap/TiledLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/tilemap/LdtkLoader.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/tilemap/AssetFileUtil.h
    ${CMAKE_SOURCE_DIR}/src/preview/atlas/AtlasDocument.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/atlas/AtlasLoader.cpp
)
add_test(NAME TwoDFormats COMMAND test_2d_formats)

# --- Media preview: waveform envelope, audio headers, media sniffers ---
qt_add_executable(test_media_formats test_media_formats.cpp)
target_include_directories(test_media_formats PRIVATE ${CMAKE_SOURCE_DIR}/src/preview/media)
target_link_libraries(test_media_formats PRIVATE Qt6::Core Qt6::Test)
target_sources(test_media_formats PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preview/media/WaveformPeaks.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/media/AudioFileInfo.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/media/MediaSniffers.cpp
)
add_test(NAME MediaFormats COMMAND test_media_formats)

# --- Font preview: sfnt / TTC / WOFF containers, tables, Unicode blocks ---
qt_add_executable(test_font_file test_font_file.cpp)
target_include_directories(test_font_file PRIVATE ${CMAKE_SOURCE_DIR}/src/preview/font)
target_link_libraries(test_font_file PRIVATE Qt6::Core Qt6::Test)
target_sources(test_font_file PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preview/font/FontFile.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/font/UnicodeBlocks.cpp
)
add_test(NAME FontFile COMMAND test_font_file)

# --- Hex view: page cache, offsets / patterns / copy formats, chunked search ---
qt_add_executable(test_hex_view test_hex_view.cpp)
target_include_directories(test_hex_view PRIVATE ${CMAKE_SOURCE_DIR}/src/preview/hex)
target_link_libraries(test_hex_view PRIVATE Qt6::Core Qt6::Test)
target_sources(test_hex_view PRIVATE
    ${CMAKE_SOURCE_DIR}/src/preview/hex/HexSource.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/hex/HexFormat.cpp
    ${CMAKE_SOURCE_DIR}/src/preview/hex/HexSearch.cpp
)
add_test(NAME HexView COMMAND test_hex_view)

# --- Media plugin end to end: QPluginLoader, factory, audio / PDF views (offscreen) ---
if(TARGET NotepadAIMedia)
    qt_add_executable(test_media_plugin test_media_plugin.cpp)
    target_include_directories(test_media_plugin PRIVATE ${CMAKE_SOURCE_DIR}/src/preview/media)
    target_link_libraries(test_media_plugin PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
    target_compile_definitions(test_media_plugin PRIVATE
        NOTEPADAI_MEDIA_PLUGIN_PATH="$<TARGET_FILE:NotepadAIMedia>")
    if(TARGET Qt6::Pdf AND TARGET Qt6::PdfWidgets)
        target_compile_definitions(test_media_plugin PRIVATE NOTEPADAI_MEDIA_HAS_PDF)
    endif()
    add_dependencies(test_media_plugin NotepadAIMedia)
    add_test(NAME MediaPlugin COMMAND test_media_plugin)
    set_tests_properties(MediaPlugin PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endif()

# --- bench_diff_view_apply: manual timing of the diff view's UI-thread work
#     (apply, first paint, page scroll, expand) on a real ScintillaNext under
#     the offscreen platform. No ctest. Numbers go in the redesign-diff-reading
#     design (D14). ---
qt_add_executable(bench_diff_view_apply bench_diff_view_apply.cpp)
target_include_directories(bench_diff_view_apply PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/git
    ${CMAKE_SOURCE_DIR}/src/widgets
    ${CMAKE_SOURCE_DIR}/thirdparty/scintilla/include
    ${CMAKE_SOURCE_DIR}/thirdparty/lexilla/include
)
target_link_libraries(bench_diff_view_apply PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent scintilla lexilla uchardet)
target_sources(bench_diff_view_apply PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/ScintillaNext.cpp
    ${CMAKE_SOURCE_DIR}/src/ScintillaNext.h
    ${CMAKE_SOURCE_DIR}/src/RangeAllocator.cpp
    ${CMAKE_SOURCE_DIR}/src/RangeAllocator.h
    ${CMAKE_SOURCE_DIR}/src/ScintillaCommenter.cpp
    ${CMAKE_SOURCE_DIR}/src/ScintillaCommenter.h
    ${CMAKE_SOURCE_DIR}/src/SelectionTracker.cpp
    ${CMAKE_SOURCE_DIR}/src/SelectionTracker.h
    ${CMAKE_SOURCE_DIR}/src/UndoAction.cpp
    ${CMAKE_SOURCE_DIR}/src/UndoAction.h
    ${CMAKE_SOURCE_DIR}/src/ByteArrayUtils.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
    ${CMAKE_SOURCE_DIR}/src/remote/IFileSystemBackend.h
    ${CMAKE_SOURCE_DIR}/src/remote/SshProfile.h
    ${CMAKE_SOURCE_DIR}/src/QRegexSearch.cpp
    ${CMAKE_SOURCE_DIR}/src/QRegexSearch.h
    ${CMAKE_SOURCE_DIR}/src/DarkPalette.h
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalIcons.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/TerminalIcons.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffDocument.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffDocumentBuilder.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffDocumentBuilder.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileClassifier.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileTreeModel.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffFileTreeModel.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffLanguages.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffLanguages.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffLanguagesLua.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffStickyHeader.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffStickyHeader.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffView.cpp
    ${CMAKE_SOURCE_DIR}/src/git/DiffView.h
    ${CMAKE_SOURCE_DIR}/src/git/DiffViewApp.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffPalette.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffParser.h
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffSyntaxMapper.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitDiffSyntaxMapper.h
    ${CMAKE_SOURCE_DIR}/src/git/GitNumstatParser.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitNumstatParser.h
    ${CMAKE_SOURCE_DIR}/src/git/GitRelativeTime.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitRelativeTime.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusItemDelegate.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusItemDelegate.h
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusModel.cpp
    ${CMAKE_SOURCE_DIR}/src/git/GitStatusModel.h
)

# --- Control API (docs/adr/0006) ---
qt_add_executable(test_control_http test_control_http.cpp)
target_include_directories(test_control_http PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_control_http PRIVATE Qt6::Core Qt6::Test)
target_sources(test_control_http PRIVATE
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.h
)
add_test(NAME ControlHttp COMMAND test_control_http)

qt_add_executable(test_control_auth test_control_auth.cpp)
target_include_directories(test_control_auth PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_control_auth PRIVATE Qt6::Core Qt6::Test)
target_sources(test_control_auth PRIVATE
    ${CMAKE_SOURCE_DIR}/src/control/ControlAuth.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlAuth.h
)
add_test(NAME ControlAuth COMMAND test_control_auth)

qt_add_executable(test_control_server test_control_server.cpp)
target_include_directories(test_control_server PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_control_server PRIVATE Qt6::Core Qt6::Network Qt6::Test)
target_sources(test_control_server PRIVATE
    ${CMAKE_SOURCE_DIR}/src/control/ControlAuth.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlAuth.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlServer.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlServer.h
)
add_test(NAME ControlServer COMMAND test_control_server)

qt_add_executable(test_control_action_index test_control_action_index.cpp)
target_include_directories(test_control_action_index PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_control_action_index PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_control_action_index PRIVATE
    ${CMAKE_SOURCE_DIR}/src/control/ControlActionIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlActionIndex.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.h
)
add_test(NAME ControlActionIndex COMMAND test_control_action_index)
set_tests_properties(ControlActionIndex PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_control_ui_tree test_control_ui_tree.cpp)
target_include_directories(test_control_ui_tree PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_control_ui_tree PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_control_ui_tree PRIVATE
    ${CMAKE_SOURCE_DIR}/src/control/ControlActionIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlActionIndex.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlAdapter.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlAudit.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlAudit.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlStandardAdapters.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlUiTree.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlUiTree.h
)
add_test(NAME ControlUiTree COMMAND test_control_ui_tree)
set_tests_properties(ControlUiTree PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

qt_add_executable(test_control_events test_control_events.cpp)
target_include_directories(test_control_events PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_control_events PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_control_events PRIVATE
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlEvents.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEvents.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.h
)
add_test(NAME ControlEvents COMMAND test_control_events)
set_tests_properties(ControlEvents PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")

# The Control API end to end over real HTTP against a test window.
qt_add_executable(test_control_api test_control_api.cpp)
target_include_directories(test_control_api PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_control_api PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Network Qt6::Concurrent Qt6::Test)
target_sources(test_control_api PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlActionIndex.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlActivityLog.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlActivityLog.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlApi.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlApi.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlAudit.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlAuth.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEffects.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEvents.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlEvents.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlFeedback.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlFeedback.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlHttp.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlIds.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlPicker.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlScreenshot.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlServer.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlServer.h
    ${CMAKE_SOURCE_DIR}/src/control/ControlStandardAdapters.cpp
    ${CMAKE_SOURCE_DIR}/src/control/ControlUiTree.cpp
    ${CMAKE_SOURCE_DIR}/src/control/control.qrc
)
add_test(NAME ControlApi COMMAND test_control_api)
set_tests_properties(ControlApi PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen" TIMEOUT 120)

# Every picker goes through control::picker (docs/adr/0006).
qt_add_executable(test_control_picker_seam test_control_picker_seam.cpp)
target_link_libraries(test_control_picker_seam PRIVATE Qt6::Core Qt6::Test)
target_compile_definitions(test_control_picker_seam PRIVATE NOTEPADAI_SRC_DIR="${CMAKE_SOURCE_DIR}/src")
add_test(NAME ControlPickerSeam COMMAND test_control_picker_seam)

# The rule for new features: every action and interactive widget of the real
# main window has a stable id. Runs the app offscreen in a throwaway data dir.
# A workspace (this checkout, a git repo) so the workspace panel and its Git tab are audited too.
add_test(NAME ControlApiAudit COMMAND $<TARGET_FILE:NotepadAI> --control-api-audit --workspace ${CMAKE_SOURCE_DIR})
set_tests_properties(ControlApiAudit PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen;NOTEPADAI_DATA_DIR=${CMAKE_BINARY_DIR}/control-api-audit-data"
    TIMEOUT 120)
