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

# --- DarkPalette: pure foreground-lightening transform for dark editor bg.
#     Header-only, so we only need the test source itself. ---
qt_add_executable(test_dark_palette test_dark_palette.cpp)
target_include_directories(test_dark_palette PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_dark_palette PRIVATE Qt6::Core Qt6::Gui Qt6::Test)
add_test(NAME DarkPalette COMMAND test_dark_palette)

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

# --- TerminalWidget key routing: regression for Tab/Backtab being swallowed by
#     QWidget::event() focus traversal before reaching keyPressEvent.
#     Subclasses TerminalWidget to inspect the routing without starting a PTY. ---
qt_add_executable(test_terminal_key_routing test_terminal_key_routing.cpp)
target_include_directories(test_terminal_key_routing PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_terminal_key_routing PRIVATE
    Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test
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
)
add_test(NAME TerminalKeyRouting COMMAND test_terminal_key_routing)

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

# --- AcpHistoryStore: debounced atomic per-session JSON writes ---
qt_add_executable(test_acp_history_store test_acp_history_store.cpp)
target_include_directories(test_acp_history_store PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_acp_history_store PRIVATE Qt6::Core Qt6::Test)
target_sources(test_acp_history_store PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
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
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
)
add_test(NAME AcpSessionModel COMMAND test_acp_session_model)

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
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
)
add_test(NAME AcpSessionModelPersistenceIntegration COMMAND test_acp_session_model_persistence_integration)

# --- AcpAgentManager: lifecycle, history worker-thread placement, shutdown ---
qt_add_executable(test_acp_agent_manager test_acp_agent_manager.cpp)
target_include_directories(test_acp_agent_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/docks
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_agent_manager PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_agent_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.h
)
add_test(NAME AcpAgentManager COMMAND test_acp_agent_manager)

# --- AcpAgentManager exit + restart: W4 session state machine ---
qt_add_executable(test_acp_agent_exit_restart test_acp_agent_exit_restart.cpp)
target_include_directories(test_acp_agent_exit_restart PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/docks
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_agent_exit_restart PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_agent_exit_restart PRIVATE
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentManager.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.h
)
add_test(NAME AcpAgentExitRestart COMMAND test_acp_agent_exit_restart)

# --- AiAgentDock: closeEvent prompt-while-running confirmation gate ---
qt_add_executable(test_acp_dock_close_event test_acp_dock_close_event.cpp)
target_include_directories(test_acp_dock_close_event PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/docks
    ${CMAKE_SOURCE_DIR}/src/widgets
)
target_link_libraries(test_acp_dock_close_event PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_acp_dock_close_event PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/ApplicationSettings.h
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpConnection.h
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpErrorClassifier.h
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpHistoryStore.h
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpProtocol.h
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.cpp
    ${CMAKE_SOURCE_DIR}/src/docks/AiAgentDock.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpImageAttachmentList.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPermissionPrompt.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpPlanWidget.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpSessionView.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.h
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.cpp
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpUsageIndicator.h
)
add_test(NAME AcpDockCloseEvent COMMAND test_acp_dock_close_event)

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

# --- AcpMessageWidget: streaming markdown + thought collapse ---
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
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpMessageWidget.h
)
add_test(NAME AcpMessageWidgetStreaming COMMAND test_acp_message_widget_streaming)
