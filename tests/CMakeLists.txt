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
    ${CMAKE_SOURCE_DIR}/src/AcpSessionModel.h
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
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
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
)
add_test(NAME AcpSessionModelPersistenceIntegration COMMAND test_acp_session_model_persistence_integration)

# --- AcpAgentManager: lifecycle, history worker-thread placement, shutdown ---
qt_add_executable(test_acp_agent_manager test_acp_agent_manager.cpp)
target_include_directories(test_acp_agent_manager PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/src/dialogs
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
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
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
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
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
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.cpp
    ${CMAKE_SOURCE_DIR}/src/ProfileScope.h
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
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
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
    ${CMAKE_SOURCE_DIR}/src/DataPaths.cpp
    ${CMAKE_SOURCE_DIR}/src/DataPaths.h
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
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgent.h
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalAgentSettings.h
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalActionParser.h
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.cpp
    ${CMAKE_SOURCE_DIR}/src/dialogs/SendWithGoalDialog.h
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
