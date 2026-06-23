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

# --- GoalDraftGenerator: XML parse + transient connection teardown ---
qt_add_executable(test_goal_draft_generator test_goal_draft_generator.cpp)
target_include_directories(test_goal_draft_generator PRIVATE ${CMAKE_SOURCE_DIR}/src)
target_link_libraries(test_goal_draft_generator PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Test)
target_sources(test_goal_draft_generator PRIVATE
    ${CMAKE_SOURCE_DIR}/src/AcpAgentDefinition.h
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.cpp
    ${CMAKE_SOURCE_DIR}/src/AcpAgentRegistry.h
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
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/GoalPromptRenderer.h
    ${CMAKE_SOURCE_DIR}/src/IAcpProcessChannel.h
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.cpp
    ${CMAKE_SOURCE_DIR}/src/LocalAcpProcessChannel.h
)
add_test(NAME GoalDraftGenerator COMMAND test_goal_draft_generator)

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
    ${CMAKE_SOURCE_DIR}/src/widgets/AcpToolCallCard.h
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

# --- MarkdownRenderer: md4c HTML output, truncation, fence mapping, anchors, highlighting ---
qt_add_executable(test_markdown_renderer test_markdown_renderer.cpp)
target_include_directories(test_markdown_renderer PRIVATE
    ${CMAKE_SOURCE_DIR}/src
    ${CMAKE_SOURCE_DIR}/thirdparty/scintilla/include
    ${CMAKE_SOURCE_DIR}/thirdparty/lexilla/include
    ${CMAKE_SOURCE_DIR}/thirdparty/lexilla/access
)
target_link_libraries(test_markdown_renderer PRIVATE Qt6::Core Qt6::Gui Qt6::Test md4c lexilla)
target_sources(test_markdown_renderer PRIVATE
    ${CMAKE_SOURCE_DIR}/src/MarkdownRenderer.cpp
    ${CMAKE_SOURCE_DIR}/src/MarkdownRenderer.h
)
add_test(NAME MarkdownRenderer COMMAND test_markdown_renderer)

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
)
add_test(NAME AsyncEditorLoad COMMAND test_async_editor_load)

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
