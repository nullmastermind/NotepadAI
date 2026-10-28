qt_add_executable(NotepadAI
	WIN32
	MACOSX_BUNDLE
	AcpAgentBrand.h
	AcpAgentDefinition.h
	AcpAgentIcons.h
	AcpAgentManager.h
	AcpAgentPicker.h
	AcpAgentRegistry.h
	AcpConnection.h
	AcpConversationCopy.h
	AcpErrorClassifier.h
	AcpHistoryStore.h
	AcpOpenSessions.h
	AcpPrewalk.h
	AcpPromptQueue.h
	AcpScheduledJobs.h
	AcpProtocol.h
	AcpSessionModel.h
	AcpSwitchAgent.h
	ClaudeSessionFiles.h
	AcpDiffStat.h
	AcpTranscriptTruncation.h
	AcpTurnFolding.h
	markdown/MarkdownPreprocessor.h
	markdown/MathLayout.h
	markdown/MathParser.h
	markdown/MathRenderCache.h
	ActionUtils.h
	ApplicationSettings.h
	CronExpression.h
	GoalActionParser.h
	GoalAgent.h
	GoalAgentSettings.h
	ProjectGoalPresets.h
	GoalHttpJudge.h
	GoalHttpJudgeRunner.h
	GoalHttpJudgeSession.h
	GoalConversationSummary.h
	GoalDraftGenerator.h
	GoalPromptRenderer.h
	GoalVerdictMcp.h
	McpStdio.h
	GrillMeForm.h
	GrillMeMcp.h
	PiAcpJudgeConfig.h
	BrowserProxyArgs.h
	QuickBrowseUrl.h
	FaviconIcon.h
	BrowserTabPin.h
	DockTabReorder.h
	ByteArrayUtils.h
	ColorPickerDelegate.h
	ComboBoxDelegate.h
	Converter.h
	CrashContext.h
	CrashHandler.h
	DataPaths.h
	DebugManager.h
	DefaultDirectoryManager.h
	DockAreaSanitizer.h
	DockBottomToolTab.h
	DockedEditor.h
	DockedEditorTitleBar.h
	IAcpExecHost.h
	IAcpProcessChannel.h
	LocalAcpProcessChannel.h
	SshAcpProcessChannel.h
	SshConnectionExecHost.h

	EditorManager.h
	EditorPrintPreviewRenderer.h
	FileDialogHelpers.h
	FileIndexCache.h
	FileWatcher.h
	WatchedPathDelete.h
	Finder.h
	FocusWatcher.h
	HtmlConverter.h
	HtmlPreviewWidget.h
	CsvPreviewWidget.h
	CsvTableModel.h
	CsvDocument.h
	CsvSortFilter.h
	CsvFilterStrip.h
	IFaceTable.h
	MarkdownPreviewSupport.h
	MarkdownPreviewWidget.h
	PandocExporter.h
	PreviewContentWidget.h
	PreviewTabManager.h
	IFaceTableMixer.h
	ISearchResultsHandler.h
	FolderSearchEngine.h
	LanguageStylesModel.h
	LuaExtension.h
	LuaState.h
	Macro.h
	MacroListModel.h
	MacroManager.h
	MacroRecorder.h
	MacroStep.h
	MacroStepTableModel.h
	NotepadNextApplication.h
	NppImporter.h
	QRegexSearch.h
	RangeAllocator.h
	RecentFilesListManager.h
	RecentFilesListMenuBuilder.h
	RtfConverter.h
	SciIFaceTable.h
	ScintillaCommenter.h
	ScintillaEnums.h
	ScintillaNext.h
	ScheduledTaskDefinition.h
	ScheduledTaskRegistry.h
	ScheduledTaskRunner.h
	SearchResultData.h
	SearchResultHighlighterDelegate.h
	SearchResultsCollector.h
	SelectionTracker.h
	SessionManager.h
	SpinBoxDelegate.h
	TranslationManager.h
	UndoAction.h
	WorkspaceFileEnumerator.h
	ZoomEventWatcher.h
	ThemeResolver.h
	StylesheetResource.h
	DarkPalette.h
	ProfileScope.h
	ShutdownDiagnostics.h
	resources.qrc
	scripts.qrc
	prompts/prompts.qrc
	ApplicationSettings.cpp
	AcpAgentManager.cpp
	AcpAgentIcons.cpp
	AcpAgentRegistry.cpp
	AcpConnection.cpp
	AcpConversationCopy.cpp
	AcpErrorClassifier.cpp
	AcpHistoryStore.cpp
	AcpOpenSessions.cpp
	AcpPrewalk.cpp
	AcpPromptQueue.cpp
	AcpScheduledJobs.cpp
	AcpProtocol.cpp
	AcpSessionModel.cpp
	AcpSwitchAgent.cpp
	ClaudeSessionFiles.cpp
	AcpDiffStat.cpp
	AcpTranscriptTruncation.cpp
	AcpTurnFolding.cpp
	markdown/MarkdownPreprocessor.cpp
	markdown/MathLayout.cpp
	markdown/MathParser.cpp
	markdown/MathRenderCache.cpp
	LocalAcpProcessChannel.cpp
	SshAcpProcessChannel.cpp
	CronExpression.cpp
	GoalActionParser.cpp
	GoalAgent.cpp
	GoalAgentSettings.cpp
	ProjectGoalPresets.cpp
	GoalHttpJudge.cpp
	GoalHttpJudgeRunner.cpp
	GoalHttpJudgeSession.cpp
	GoalConversationSummary.cpp
	GoalDraftGenerator.cpp
	GoalPromptRenderer.cpp
	GoalVerdictMcp.cpp
	McpStdio.cpp
	GrillMeForm.cpp
	GrillMeMcp.cpp
	PiAcpJudgeConfig.cpp
	BrowserProxyArgs.cpp
	QuickBrowseUrl.cpp
	FaviconIcon.cpp
	BrowserTabPin.cpp
	DockTabReorder.cpp
	ColorPickerDelegate.cpp
	ComboBoxDelegate.cpp
	Converter.cpp
	CrashContext.cpp
	CrashHandler.cpp
	DataPaths.cpp
	DebugManager.cpp
	DefaultDirectoryManager.cpp
	DockAreaSanitizer.cpp
	DockBottomToolTab.cpp
	DockedEditor.cpp

	EditorManager.cpp
	EditorPrintPreviewRenderer.cpp
	FileDialogHelpers.cpp
	FileWatcher.cpp
	WatchedPathDelete.cpp
	Finder.cpp
	HtmlConverter.cpp
	HtmlPreviewWidget.cpp
	CsvPreviewWidget.cpp
	CsvTableModel.cpp
	CsvEditCommands.cpp
	CsvDocument.cpp
	CsvSortFilter.cpp
	CsvFilterStrip.cpp
	IFaceTable.cpp
	MarkdownPreviewWidget.cpp
	PandocExporter.cpp
	PreviewTabManager.cpp
	IFaceTableMixer.cpp
	LanguageStylesModel.cpp
	LuaExtension.cpp
	LuaState.cpp
	Macro.cpp
	MacroListModel.cpp
	MacroManager.cpp
	MacroRecorder.cpp
	MacroStep.cpp
	MacroStepTableModel.cpp
	main.cpp
	NotepadNextApplication.cpp
	NppImporter.cpp
	QRegexSearch.cpp
	RangeAllocator.cpp
	RecentFilesListManager.cpp
	RecentFilesListMenuBuilder.cpp
	RtfConverter.cpp
	SciIFaceTable.cpp
	ScintillaCommenter.cpp
	ScintillaNext.cpp
	ScheduledTaskRegistry.cpp
	ScheduledTaskRunner.cpp
	SearchResultHighlighterDelegate.cpp
	SearchResultsCollector.cpp
	FolderSearchEngine.cpp
	SelectionTracker.cpp
	SessionManager.cpp
	SpinBoxDelegate.cpp
	TranslationManager.cpp
	UndoAction.cpp
	WorkspaceFileEnumerator.cpp
	ZoomEventWatcher.cpp
	ProfileScope.cpp
	ShutdownDiagnostics.cpp
	decorators/ApplicationDecorator.cpp
	decorators/ApplicationDecorator.h
	decorators/AutoCompletion.cpp
	decorators/AutoCompletion.h
	decorators/AutoIndentation.cpp
	decorators/AutoIndentation.h
	decorators/BetterMultiSelection.cpp
	decorators/BetterMultiSelection.h
	decorators/BookMarkDecorator.cpp
	decorators/BookMarkDecorator.h
	decorators/BraceMatch.cpp
	decorators/BraceMatch.h
	decorators/EditorConfigAppDecorator.cpp
	decorators/EditorConfigAppDecorator.h
	decorators/EditorDecorator.cpp
	decorators/EditorDecorator.h
	decorators/GitGutterDecorator.cpp
	decorators/GitGutterDecorator.h
	decorators/GitGutterDiffPopup.cpp
	decorators/GitGutterDiffPopup.h
	decorators/GitGutterMarkers.cpp
	decorators/GitGutterMarkers.h
	decorators/HighlightedScrollBar.cpp
	decorators/HighlightedScrollBar.h
	decorators/InlineBlameDecorator.cpp
	decorators/InlineBlameDecorator.h
	decorators/JustfileRecipeHighlighter.cpp
	decorators/JustfileRecipeHighlighter.h
	decorators/HTMLAutoCompleteDecorator.cpp
	decorators/HTMLAutoCompleteDecorator.h
	decorators/LineNumbers.cpp
	decorators/LineNumbers.h
	decorators/MarkerAppDecorator.cpp
	decorators/MarkerAppDecorator.h
	decorators/SmartHighlighter.cpp
	decorators/SmartHighlighter.h
	decorators/SurroundSelection.cpp
	decorators/SurroundSelection.h
	decorators/URLFinder.cpp
	decorators/URLFinder.h
	dialogs/AcpAgentSettingsDialog.cpp
	dialogs/AcpAgentSettingsDialog.h
	dialogs/AcpAgentSettingsDialog.ui
	dialogs/GoalDraftDialog.cpp
	dialogs/GoalDraftDialog.h
	dialogs/SendWithGoalDialog.cpp
	dialogs/SendWithGoalDialog.h
	dialogs/ScheduledTaskDialog.cpp
	dialogs/ScheduledTaskDialog.h
	dialogs/EditScheduledTaskDialog.cpp
	dialogs/EditScheduledTaskDialog.h
	dialogs/GoalAgentConfigDialog.cpp
	dialogs/GoalAgentConfigDialog.h
	dialogs/EditTasksDialog.cpp
	dialogs/EditTasksDialog.h
	dialogs/EditMiniAppsDialog.cpp
	dialogs/EditMiniAppsDialog.h
	dialogs/ColumnEditorDialog.cpp
	dialogs/ColumnEditorDialog.h
	dialogs/ColumnEditorDialog.ui
	dialogs/FindReplaceDialog.cpp
	dialogs/FindReplaceDialog.h
	dialogs/FindReplaceDialog.ui
	dialogs/MacroEditorDialog.cpp
	dialogs/MacroEditorDialog.h
	dialogs/MacroEditorDialog.ui
	dialogs/MacroRunDialog.cpp
	dialogs/MacroRunDialog.h
	dialogs/MacroRunDialog.ui
	dialogs/MacroSaveDialog.cpp
	dialogs/MacroSaveDialog.h
	dialogs/MacroSaveDialog.ui
	dialogs/MainWindow.cpp
	dialogs/MainWindow.h
	dialogs/MainWindow.ui
	dialogs/QuickFileOpenDialog.cpp
	dialogs/QuickFileOpenDialog.h
	dialogs/RecentWorkspaceOpenDialog.cpp
	dialogs/RecentWorkspaceOpenDialog.h
	dialogs/InteractiveRebaseDialog.cpp
	dialogs/InteractiveRebaseDialog.h
	dialogs/PreferencesDialog.cpp
	dialogs/PreferencesDialog.h
	dialogs/PreferencesDialog.ui
	dialogs/PreferencesPendingEdits.h
	dialogs/UnfocusedWheelFilter.h
	dialogs/SshConnectionManagerDialog.cpp
	dialogs/SshConnectionManagerDialog.h
	dialogs/SshConnectDialog.cpp
	dialogs/SshConnectDialog.h
	dialogs/SshRemoteFolderPickerDialog.cpp
	dialogs/SshRemoteFolderPickerDialog.h
	dialogs/SshDebugDialog.h
	dialogs/SshDebugDialog.cpp
	docks/AiAgentDock.cpp
	docks/AiAgentDock.h
	docks/DebugLogDock.cpp
	docks/DebugLogDock.h
	docks/DebugLogDock.ui
	docks/EditorInspectorDock.cpp
	docks/EditorInspectorDock.h
	docks/EditorInspectorDock.ui
	docks/FileListDock.cpp
	docks/FileListDock.h
	docks/FileListDock.ui
	docks/FolderAsWorkspaceDock.cpp
	docks/FolderAsWorkspaceDock.h
	docks/FolderAsWorkspaceDock.ui
	docks/FolderAsWorkspaceFsModel.cpp
	docks/FolderAsWorkspaceFsModel.h
	docks/FolderAsWorkspaceProxyModel.cpp
	docks/FolderAsWorkspaceProxyModel.h

	git/BranchPickerPopup.cpp
	git/BranchPickerPopup.h
	git/WorktreePickerPopup.cpp
	git/WorktreePickerPopup.h
	git/BranchRefParser.cpp
	git/BranchRefParser.h
	git/BufferDiffEngine.cpp
	git/BufferDiffEngine.h
	git/CatFileBlobFetcher.cpp
	git/CatFileBlobFetcher.h
	git/ChangesPanel.cpp
	git/ChangesPanel.h
	git/CommitComposer.cpp
	git/CommitComposer.h
	git/CommitSubmitPolicy.h
	git/DiffDocument.h
	git/DiffDocumentBuilder.cpp
	git/DiffDocumentBuilder.h
	git/DiffFileClassifier.cpp
	git/DiffFileClassifier.h
	git/DiffFileTreeModel.cpp
	git/DiffFileTreeModel.h
	git/DiffLanguages.cpp
	git/DiffLanguages.h
	git/DiffLanguagesLua.cpp
	git/DiffLanguagesLua.h
	git/DiffStickyHeader.cpp
	git/DiffStickyHeader.h
	git/DiffView.cpp
	git/DiffView.h
	git/DiffViewApp.cpp
	git/DiffViewApp.h
	git/FileEncodingDetector.cpp
	git/FileEncodingDetector.h
	git/ForceRemovePath.cpp
	git/ForceRemovePath.h
	git/GitBlameParser.cpp
	git/GitBlameParser.h
	git/GitBlameFetcher.cpp
	git/GitBlameFetcher.h
	git/GitController.cpp
	git/GitController.h
	git/GitRefreshCoalescer.h
	git/GitCommitDetail.h
	git/GitCommitFetcher.cpp
	git/GitCommitFetcher.h
	git/GitCommitInfo.h
	git/GitCommitView.cpp
	git/GitCommitView.h
	git/GitDiffCache.cpp
	git/GitDiffCache.h
	git/GitDiffFetcher.cpp
	git/GitDiffFetcher.h
	git/GitDiffPalette.cpp
	git/GitDiffPalette.h
	git/GitDiffParser.cpp
	git/GitDiffParser.h
	git/GitDiffSyntaxMapper.cpp
	git/GitDiffSyntaxMapper.h
	git/GitDiffViewController.cpp
	git/GitDiffViewController.h
	git/GitError.h
	git/GitErrorClassifier.cpp
	git/GitErrorClassifier.h
	git/GitBaseBlobCache.cpp
	git/GitBaseBlobCache.h
	git/GitFileAtShaFetcher.cpp
	git/GitFileAtShaFetcher.h
	git/GitHistoryFetcher.cpp
	git/GitHistoryFetcher.h
	git/GitHistoryItemDelegate.cpp
	git/GitHistoryItemDelegate.h
	git/GitHistoryModel.cpp
	git/GitHistoryModel.h
	git/GitHistoryView.cpp
	git/GitHistoryView.h
	git/GitLogParser.cpp
	git/GitLogParser.h
	git/GitNumstatParser.cpp
	git/GitNumstatParser.h
	git/GitProcessRunner.cpp
	git/GitProcessRunner.h
	git/GitRelativeTime.cpp
	git/GitRelativeTime.h
	git/GitRunnerFactory.cpp
	git/GitRunnerFactory.h
	git/PathStatusIndex.cpp
	git/PathStatusIndex.h
	git/SubmoduleStatusFetcher.cpp
	git/SubmoduleStatusFetcher.h
	git/GitTabSegmentedBar.cpp
	git/GitTabSegmentedBar.h
	git/GitRepoDiscovery.cpp
	git/GitRepoDiscovery.h
	git/GitRepoInfo.h
	git/GitRepoModel.cpp
	git/GitRepoModel.h
	git/GitStatusEntry.h
	git/GitStatusItemDelegate.cpp
	git/GitStatusItemDelegate.h
	git/GitStatusModel.cpp
	git/GitStatusModel.h
	git/GitStatusParser.cpp
	git/GitStatusParser.h
	git/GitStatusSync.cpp
	git/GitStatusSync.h
	git/GitTabWidget.cpp
	git/GitTabWidget.h
	git/GitTypes.cpp
	git/GitWatcher.cpp
	git/GitWatcher.h
	git/RecursiveTreeWatcher.cpp
	git/RecursiveTreeWatcher.h
	git/RecursiveTreeWatcherLogic.h
	git/ConflictEntry.h
	git/GitOperationManager.cpp
	git/GitOperationManager.h
	git/SyncScrollMap.cpp
	git/SyncScrollMap.h

	docks/LanguageInspectorDock.cpp
	docks/LanguageInspectorDock.h
	docks/LanguageInspectorDock.ui
	docks/ConflictListDock.cpp
	docks/ConflictListDock.h
	docks/ConflictMergeViewerDock.cpp
	docks/ConflictMergeViewerDock.h
	docks/LuaConsoleDock.cpp
	docks/LuaConsoleDock.h
	docks/LuaConsoleDock.ui
	docks/SearchResultsDock.cpp
	docks/SearchResultsDock.h
	docks/SearchResultsDock.ui
	docks/FindInFolderDock.cpp
	docks/FindInFolderDock.h
	docks/FindInFolderDock.ui
    widgets/AcpImageAttachmentList.cpp
	widgets/AcpImageAttachmentList.h
	widgets/AcpMessageWidget.cpp
	widgets/MarkdownView.cpp
	widgets/AcpMessageWidget.h
	widgets/ChatFont.h
	widgets/MarkdownView.h
	widgets/AcpPermissionPrompt.cpp
	widgets/AcpPermissionPrompt.h
	widgets/AcpPlanWidget.cpp
	widgets/AcpPlanWidget.h
	widgets/AcpPromptQueueStrip.cpp
	widgets/AcpPromptQueueStrip.h
	widgets/AcpSessionView.cpp
	widgets/GrillMeFormWidget.cpp
	widgets/AcpSessionView.h
	widgets/GrillMeFormWidget.h
	widgets/AcpToolCallCard.cpp
	widgets/AcpToolCallCard.h
	widgets/AcpTurnFoldRow.cpp
	widgets/AcpTurnFoldRow.h
	widgets/AcpUsageIndicator.cpp
	widgets/AcpUsageIndicator.h
	widgets/UsageButton.cpp
	widgets/UsageButton.h
	widgets/UsagePopover.cpp
	widgets/UsagePopover.h
	widgets/UsageStyle.h
	widgets/UsageWindowBar.cpp
	widgets/UsageWindowBar.h
	widgets/AiDockSessionStrip.cpp
	widgets/AiDockSessionStrip.h
	widgets/AiDockTitleBar.cpp
	widgets/AiDockTitleBar.h
    widgets/DockMiddleClickCloser.cpp
	widgets/DockMiddleClickCloser.h
    widgets/DockTabContextMenu.cpp
	widgets/DockTabContextMenu.h
    widgets/EditorInfoStatusBar.cpp
	widgets/EditorInfoStatusBar.h
	widgets/EditorMinimap.cpp
	widgets/EditorMinimap.h
	widgets/FuzzyHighlightDelegate.cpp
	widgets/FuzzyHighlightDelegate.h
	widgets/GoalConfigWidget.cpp
	widgets/GoalConfigWidget.h
	widgets/GoalCustomApiFields.cpp
	widgets/GoalCustomApiFields.h
	widgets/MinimapMath.h
	widgets/TaskRunnerGutter.cpp
	widgets/TaskRunnerGutter.h
	widgets/FadingIndicator.cpp
	widgets/FadingIndicator.h
	widgets/QuickFindWidget.cpp
	widgets/QuickFindWidget.h
	widgets/QuickFindWidget.ui
	widgets/StatusLabel.cpp
	widgets/StatusLabel.h
	Sorter.h Sorter.cpp
	ScintillaSorter.h ScintillaSorter.cpp
	widgets/TabsQuickActionsBar.cpp
	widgets/TabsQuickActionsBar.h
	widgets/WebViewWidget.h
	widgets/WebViewWidget.cpp

	TerminalAiHelper.h
	TerminalAiHelper.cpp
	TerminalCwdResolver.h
	TerminalTabTitle.h
	TerminalTaskLauncher.h
	TerminalTaskLauncher.cpp
	AiDockGroup.h
	TerminalCwdResolver.cpp
	TerminalProcessStats.h
	TerminalProcessStats.cpp

	EditorThemeColors.h
	TerminalManager.h
	TerminalManager.cpp
	TerminalEmptyPanelPolicy.h
	TerminalEmptyPanelPolicy.cpp
	WorkspaceTaskScanner.h
	WorkspaceTaskScanner.cpp
	widgets/TerminalEmptyPanel.h
	widgets/TerminalEmptyPanel.cpp
	TerminalTaskRegistry.h
	TerminalTaskRegistry.cpp
	DotEnv.h
	DotEnv.cpp
	pm2/Pm2Entrypoint.h
	pm2/Pm2Entrypoint.cpp
	pm2/Pm2Protocol.h
	pm2/Pm2Protocol.cpp
	pm2/Pm2ProcessModel.h
	pm2/Pm2ProcessModel.cpp
	pm2/Pm2Client.h
	pm2/Pm2Client.cpp
	pm2/Pm2ProcessEditDialog.h
	pm2/Pm2ProcessEditDialog.cpp
	pm2/Pm2ProcessesDialog.h
	pm2/Pm2ProcessesDialog.cpp
	pm2/pm2.qrc
	control/ControlActionIndex.h
	control/ControlActionIndex.cpp
	control/ControlActivityLog.h
	control/ControlActivityLog.cpp
	control/ControlApi.h
	control/ControlApi.cpp
	control/ControlAppAdapters.h
	control/ControlAppAdapters.cpp
	control/ControlApiDialog.h
	control/ControlScreenshot.h
	control/ControlScreenshot.cpp
	control/ControlApiDialog.cpp
	control/ControlAudit.h
	control/ControlAudit.cpp
	control/ControlAuth.h
	control/ControlAuth.cpp
	control/ControlEffects.h
	control/ControlEffects.cpp
	control/ControlEvents.h
	control/ControlEvents.cpp
	control/ControlFeedback.h
	control/ControlFeedback.cpp
	control/ControlHttp.h
	control/ControlHttp.cpp
	control/ControlIds.h
	control/ControlIds.cpp
	control/ControlServer.h
	control/ControlServer.cpp
	control/ControlStatusIndicator.h
	control/ControlStatusIndicator.cpp
	control/ControlAdapter.h
	control/ControlStandardAdapters.cpp
	control/ControlPicker.h
	control/ControlPicker.cpp
	control/ControlUiTree.h
	control/ControlUiTree.cpp
	control/control.qrc
	MiniAppDefinition.h
	MiniAppMenuLayout.h
	MiniAppMenuLayout.cpp
	MiniAppRegistry.h
	MiniAppRegistry.cpp
	MiniAppInstance.h
	MiniAppInstance.cpp
	MiniAppManager.h
	MiniAppManager.cpp
	EmbeddedWindowController.h
	EmbeddedWindowController.cpp
	EmbeddedWindowManager.h
	EmbeddedWindowManager.cpp
	EmbeddedWindowFocusRouter.h
	EmbeddedWindowWin32.h
	EmbeddedWindowWin32.cpp
	EmbeddedWindowEnumeration.cpp
	EmbeddedWindowWorker.h
	EmbeddedWindowWorker.cpp
	NativeWindowHost.h
	NativeWindowHost.cpp
	PendingCloseState.h
	widgets/TerminalColorScheme.h
	widgets/TerminalColorScheme.cpp
	widgets/ScrollbackBuffer.h
	widgets/ScrollbackBuffer.cpp
	widgets/TerminalCellSource.h
	widgets/TerminalCellSource.cpp
	widgets/TerminalRowLayout.h
	widgets/TerminalRowLayout.cpp
	widgets/TerminalBoxDrawing.h
	widgets/TerminalBoxDrawing.cpp
	widgets/TerminalGlyphCache.h
	widgets/TerminalGlyphCache.cpp
	widgets/TerminalContrast.h
	widgets/TerminalContrast.cpp
	widgets/TerminalScrollBar.h
	widgets/TerminalScrollBar.cpp
	widgets/TerminalLinkDetector.h
	widgets/TerminalLinkDetector.cpp
	widgets/TerminalSearch.h
	widgets/TerminalSearch.cpp
	widgets/TerminalSearchSession.h
	widgets/TerminalSearchSession.cpp
	widgets/TerminalSearchBar.h
	widgets/TerminalSearchBar.cpp
	widgets/TerminalIcons.h
	widgets/TerminalIcons.cpp
	widgets/TerminalWidget.h
	widgets/TerminalWidget.cpp
	docks/TerminalDock.h
	docks/TerminalDock.cpp
	docks/TerminalAreaToolbar.h
	docks/TerminalAreaToolbar.cpp

	ai/DiffCompressor.h
	ai/DiffCompressor.cpp
	ai/CommitMessageTool.h
	ai/CommitMessageTool.cpp
	ai/PromptAssembler.h
	ai/PromptAssembler.cpp
	ai/RulesLocator.h
	ai/RulesLocator.cpp
	ai/SsePartialParser.h
	ai/SsePartialParser.cpp
	ai/CredentialStore.h
	ai/CredentialStore_Common.cpp
	ai/ClaudeUsage.cpp
	ai/ClaudeUsage.h
	ai/ClaudeUsageAccounts.cpp
	ai/ClaudeUsageAccounts.h
	ai/ClaudeUsageService.cpp
	ai/ClaudeUsageService.h
	ai/IAnthropicMessagesClient.h
	ai/AnthropicMessagesClient.h
	ai/AnthropicMessagesClient.cpp
	ai/ILlmHttpClient.h
	ai/LlmHttpClient.h
	ai/LlmHttpClient.cpp
	ai/OpenaiAnthropicBridge.h
	ai/OpenaiAnthropicBridge.cpp
	ai/CommitMessageGenerator.h
	ai/CommitMessageGenerator.cpp
	ai/PromptImprover.h
	ai/PromptImprover.cpp

	remote/ExecutionContext.h
	remote/IFileSystemBackend.h
	remote/IWorkspaceFsModel.h
	remote/LocalExecutionContext.h
	remote/LocalExecutionContext.cpp
	remote/RemoteDirectoryWatcher.h
	remote/RemoteDirectoryWatcher.cpp
	remote/RemoteExecutionContext.h
	remote/RemoteExecutionContext.cpp
	remote/RemoteFileSystemModel.h
	remote/RemoteFileSystemModel.cpp
	remote/RemoteFsBackend.h
	remote/RemoteFsBackend.cpp
	remote/RemoteGitProcessRunner.h
	remote/RemoteGitProcessRunner.cpp
	remote/ExecutionContextRegistry.h
	remote/ExecutionContextRegistry.cpp
	remote/ISshTransport.h
	remote/Libssh2Transport.h
	remote/Libssh2Transport.cpp
	remote/SshChannel.h
	remote/SshChannel.cpp
	remote/SshSessionWorker.h
	remote/SshSessionWorker.cpp
	remote/SshConnection.h
	remote/SshConnection.cpp
	remote/SshPtyProcess.h
	remote/SshPtyProcess.cpp
	remote/SshProfile.h
	remote/SshProfileRegistry.h
	remote/SshProfileRegistry.cpp
	remote/SshHostKeyStore.h
	remote/SshHostKeyStore.cpp
)

# wildmatch.c is a C file. Isolate it in a tiny static lib so it doesn't pull a
# C compiler into the main target (which would cause CMake to compile the shared
# C++ PCH as C, breaking every C++ STL header).
add_library(wildmatch_c STATIC remote/wildmatch.c remote/wildmatch.h)
target_include_directories(wildmatch_c PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/remote)
target_link_libraries(NotepadAI PRIVATE wildmatch_c)

target_sources(NotepadAI PRIVATE remote/GitignoreMatcher.cpp remote/GitignoreMatcher.h)
target_sources(NotepadAI PRIVATE widgets/TransferProgressBar.cpp widgets/TransferProgressBar.h)
target_sources(NotepadAI PRIVATE dialogs/TransferConflictDialog.cpp dialogs/TransferConflictDialog.h)
target_sources(NotepadAI PRIVATE dialogs/TransferLogDialog.cpp dialogs/TransferLogDialog.h)
target_sources(NotepadAI PRIVATE remote/RemoteTransferManager.cpp remote/RemoteTransferManager.h)
target_sources(NotepadAI PRIVATE
    ZipPath.cpp ZipPath.h
    ZipIgnoreWalk.cpp ZipIgnoreWalk.h
    ZipUploadWipe.cpp ZipUploadWipe.h
    ZipArchive.cpp ZipArchive.h
    FolderZipTransfer.cpp FolderZipTransfer.h
    CrocTool.cpp CrocTool.h
    CrocShareFlow.cpp CrocShareFlow.h
)
target_link_libraries(NotepadAI PRIVATE miniz)

# Asset preview tabs (add-asset-preview-framework): resolver, viewer base,
# image/SVG viewer and texture decoders (DDS, TGA, PSD, HDR, EXR, Aseprite).
target_sources(NotepadAI PRIVATE
    preview/FileKindResolver.cpp preview/FileKindResolver.h
    preview/AssetPreviewWidget.cpp preview/AssetPreviewWidget.h
    preview/AssetPreviewTypes.cpp preview/AssetPreviewTypes.h
    preview/ImageDocument.cpp preview/ImageDocument.h
    preview/ImageViewMath.h
    preview/ImageDecoder.cpp preview/ImageDecoder.h
    preview/DdsDecoder.cpp preview/DdsDecoder.h
    preview/RasterDecoders.cpp preview/RasterDecoders.h
    preview/AsepriteDecoder.cpp preview/AsepriteDecoder.h
    preview/ImageCanvas.cpp preview/ImageCanvas.h
    preview/ImagePreviewWidget.cpp preview/ImagePreviewWidget.h
)
target_link_libraries(NotepadAI PRIVATE Qt6::Svg stb_image bcdec tinyexr)

# 3D model tabs (add-model-preview): loaders (glTF via cgltf, FBX via ufbx,
# OBJ via fast_obj, own STL/PLY/VOX) and the native OpenGL 3.3 viewer.
target_sources(NotepadAI PRIVATE
    preview/model/ModelDocument.cpp preview/model/ModelDocument.h
    preview/model/ModelLoader.cpp preview/model/ModelLoader.h
    preview/model/GltfLoader.cpp preview/model/GltfLoader.h
    preview/model/FbxLoader.cpp preview/model/FbxLoader.h
    preview/model/ObjLoader.cpp preview/model/ObjLoader.h
    preview/model/SimpleMeshLoaders.cpp preview/model/SimpleMeshLoaders.h
    preview/model/ModelAnimator.cpp preview/model/ModelAnimator.h
    preview/model/ModelCamera.cpp preview/model/ModelCamera.h
    preview/model/ModelRenderer.cpp preview/model/ModelRenderer.h
    preview/model/ModelViewWindow.cpp preview/model/ModelViewWindow.h
    preview/model/ModelPreviewWidget.cpp preview/model/ModelPreviewWidget.h
)
target_link_libraries(NotepadAI PRIVATE Qt6::OpenGL cgltf ufbx fast_obj)

# 2D game format tabs (add-2d-game-format-preview): Tiled / LDtk tile maps with
# the chunk-cached canvas, and atlases / bitmap fonts on the image canvas.
target_sources(NotepadAI PRIVATE
    preview/tilemap/TileMapDocument.cpp preview/tilemap/TileMapDocument.h
    preview/tilemap/TileMapLoader.cpp preview/tilemap/TileMapLoader.h preview/tilemap/TileMapLoaderInternal.h
    preview/tilemap/AssetFileUtil.h
    preview/tilemap/TiledLoader.cpp
    preview/tilemap/LdtkLoader.cpp
    preview/tilemap/TileMapCanvas.cpp preview/tilemap/TileMapCanvas.h
    preview/tilemap/TileMapPreviewWidget.cpp preview/tilemap/TileMapPreviewWidget.h
    preview/atlas/AtlasDocument.cpp preview/atlas/AtlasDocument.h
    preview/atlas/AtlasLoader.cpp preview/atlas/AtlasLoader.h
    preview/atlas/AtlasPreviewWidget.cpp preview/atlas/AtlasPreviewWidget.h
)

# Font tabs and the media tab shell (add-media-preview). The audio, video and
# PDF viewers are the NotepadAIMedia plugin, loaded on first use (D1), so Qt
# Multimedia and Qt Pdf never load at startup.
target_sources(NotepadAI PRIVATE
    preview/PaletteMix.h
    preview/media/MediaFormat.h
    preview/media/MediaPluginInterface.h
    preview/media/MediaPluginLoader.cpp preview/media/MediaPluginLoader.h
    preview/media/MediaPreviewWidget.cpp preview/media/MediaPreviewWidget.h
    preview/media/MediaSniffers.cpp preview/media/MediaSniffers.h
    preview/font/FontFile.cpp preview/font/FontFile.h
    preview/font/UnicodeBlocks.cpp preview/font/UnicodeBlocks.h
    preview/font/GlyphGridView.cpp preview/font/GlyphGridView.h
    preview/font/FontSampleView.cpp preview/font/FontSampleView.h
    preview/font/FontPreviewWidget.cpp preview/font/FontPreviewWidget.h
)

# Hex tab for unrecognised binaries (add-hex-preview): paged reads, no mapping.
target_sources(NotepadAI PRIVATE
    preview/hex/HexSource.cpp preview/hex/HexSource.h
    preview/hex/HexFormat.cpp preview/hex/HexFormat.h
    preview/hex/HexSearch.cpp preview/hex/HexSearch.h
    preview/hex/HexView.cpp preview/hex/HexView.h
    preview/hex/HexPreviewWidget.cpp preview/hex/HexPreviewWidget.h
)

if(TARGET Qt6::Multimedia AND TARGET Qt6::MultimediaWidgets)
    qt_add_plugin(NotepadAIMedia SHARED CLASS_NAME MediaPluginFactory
        preview/media/plugin/MediaPlugin.cpp preview/media/plugin/MediaPlugin.h
        preview/media/plugin/AudioView.cpp preview/media/plugin/AudioView.h
        preview/media/plugin/WaveformView.cpp preview/media/plugin/WaveformView.h
        preview/media/plugin/WaveformBuilder.cpp preview/media/plugin/WaveformBuilder.h
        preview/media/plugin/VideoView.cpp preview/media/plugin/VideoView.h
        preview/media/MediaPluginInterface.h
        preview/media/MediaFormat.h
        preview/media/AudioFileInfo.cpp preview/media/AudioFileInfo.h
        preview/media/WaveformPeaks.cpp preview/media/WaveformPeaks.h
    )
    target_include_directories(NotepadAIMedia PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/preview
        ${CMAKE_CURRENT_SOURCE_DIR}/preview/media
        ${CMAKE_CURRENT_SOURCE_DIR}/preview/media/plugin)
    target_link_libraries(NotepadAIMedia PRIVATE
        Qt6::Core Qt6::Gui Qt6::Widgets Qt6::Concurrent Qt6::Multimedia Qt6::MultimediaWidgets)
    if(TARGET Qt6::Pdf AND TARGET Qt6::PdfWidgets)
        target_sources(NotepadAIMedia PRIVATE preview/media/plugin/PdfView.cpp preview/media/plugin/PdfView.h)
        target_link_libraries(NotepadAIMedia PRIVATE Qt6::Pdf Qt6::PdfWidgets)
        target_compile_definitions(NotepadAIMedia PRIVATE NOTEPADAI_MEDIA_HAS_PDF)
        target_compile_definitions(NotepadAI PRIVATE NOTEPADAI_MEDIA_PDF)
    endif()
    # MediaPluginLoader looks for exactly this name next to the executable.
    if(WIN32)
        set(NOTEPADAI_MEDIA_SUFFIX ".dll")
    else()
        set(NOTEPADAI_MEDIA_SUFFIX ".so")
    endif()
    set_target_properties(NotepadAIMedia PROPERTIES
        PREFIX ""
        SUFFIX "${NOTEPADAI_MEDIA_SUFFIX}"
        LIBRARY_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:NotepadAI>"
        RUNTIME_OUTPUT_DIRECTORY "$<TARGET_FILE_DIR:NotepadAI>"
    )
    target_compile_definitions(NotepadAI PRIVATE NOTEPADAI_MEDIA_AUDIO_VIDEO)
    add_dependencies(NotepadAI NotepadAIMedia)
    if(APPLE)
        install(TARGETS NotepadAIMedia LIBRARY DESTINATION NotepadAI.app/Contents/PlugIns)
    elseif(NOT WIN32)
        install(TARGETS NotepadAIMedia LIBRARY DESTINATION lib)
    endif()
else()
    message(STATUS "Qt Multimedia not found: audio, video and PDF previews are not built")
endif()

# Platform-conditional CredentialStore backend + native API link.
if(WIN32)
    target_sources(NotepadAI PRIVATE ai/CredentialStore_Windows.cpp)
    # crypt32/dbghelp: existing deps. ws2_32: winsock for the SSH transport's
    # raw socket (Libssh2Transport.cpp calls getaddrinfo/connect/closesocket).
    # dwmapi: filters cloaked compositor windows from the Embed menu.
    target_link_libraries(NotepadAI PRIVATE crypt32 dbghelp ws2_32 dwmapi comctl32)
elseif(APPLE)
    target_sources(NotepadAI PRIVATE ai/CredentialStore_Mac.mm)
    target_link_libraries(NotepadAI PRIVATE "-framework Security" "-framework CoreFoundation")
else()
    target_sources(NotepadAI PRIVATE ai/CredentialStore_Linux.cpp)
    # libsecret resolved at runtime via QLibrary("secret-1"); no build dep.
endif()

# Platform-conditional WebView widget for Mini Apps.
if(WIN32)
    target_sources(NotepadAI PRIVATE widgets/WebViewWidget_win.cpp)
    target_link_libraries(NotepadAI PRIVATE webview2)
elseif(APPLE)
    enable_language(OBJCXX)
    target_sources(NotepadAI PRIVATE widgets/WebViewWidget_mac.mm)
    target_link_libraries(NotepadAI PRIVATE "-framework WebKit")
endif()

set_target_properties(NotepadAI PROPERTIES
    MACOSX_BUNDLE_INFO_PLIST ${CMAKE_SOURCE_DIR}/deploy/macos/info.plist
)

file(GLOB TS_FILES
	${CMAKE_SOURCE_DIR}/i18n/*.ts
)

find_package(Qt6 REQUIRED COMPONENTS LinguistTools)
qt_add_translations(NotepadAI
    TS_FILES ${TS_FILES}
    RESOURCE_PREFIX "/i18n"
)


target_include_directories(NotepadAI
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/decorators
        ${CMAKE_CURRENT_SOURCE_DIR}/dialogs
        ${CMAKE_CURRENT_SOURCE_DIR}/docks
        ${CMAKE_CURRENT_SOURCE_DIR}/git
        ${CMAKE_CURRENT_SOURCE_DIR}/preview
        ${CMAKE_CURRENT_SOURCE_DIR}/preview/model
        ${CMAKE_CURRENT_SOURCE_DIR}/preview/tilemap
        ${CMAKE_CURRENT_SOURCE_DIR}/preview/atlas
        ${CMAKE_CURRENT_SOURCE_DIR}/preview/media
        ${CMAKE_CURRENT_SOURCE_DIR}/preview/font
        ${CMAKE_CURRENT_SOURCE_DIR}/preview/hex
        ${CMAKE_CURRENT_SOURCE_DIR}/pm2
        ${CMAKE_CURRENT_SOURCE_DIR}/control
        ${CMAKE_CURRENT_SOURCE_DIR}/remote
        ${CMAKE_CURRENT_SOURCE_DIR}/widgets
        ${CMAKE_CURRENT_SOURCE_DIR}
)

if(WIN32)
	# project() leaves PROJECT_VERSION_PATCH empty when VERSION has only major.minor;
	# the Windows RC FILEVERSION field requires a numeric value, and NSIS reads this
	# back via !getdllversion to derive the installer filename, so default to 0.
	set(PROJECT_VERSION_PATCH_OR_ZERO "${PROJECT_VERSION_PATCH}")
	if(NOT PROJECT_VERSION_PATCH_OR_ZERO)
		set(PROJECT_VERSION_PATCH_OR_ZERO 0)
	endif()
	configure_file(
		"${CMAKE_SOURCE_DIR}/deploy/windows/version.rc.in"
		"${CMAKE_BINARY_DIR}/version.rc"
		@ONLY
	)
	target_sources(NotepadAI PRIVATE "${CMAKE_BINARY_DIR}/version.rc")
	configure_file(
		"${CMAKE_SOURCE_DIR}/icon/NotepadAI.ico"
		"${CMAKE_BINARY_DIR}/NotepadAI.ico"
		COPYONLY
	)
endif()

target_link_libraries(NotepadAI
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Network
        Qt6::PrintSupport
        Qt6::Concurrent
        qtadvanceddocking-qt6
        SingleApplication::SingleApplication
        editorconfig-core-qt
        lua
        lexilla
        scintilla
        uchardet
        libvterm
        libptyqt
        xxhash
        xdiff
        md4c
        supertinycron
        libssh2
        sentry
        miniz
)

target_compile_definitions(NotepadAI
    PRIVATE
        ADS_STATIC
        QAPPLICATION_CLASS=QApplication
        APP_VERSION="${PROJECT_VERSION}"
        APP_COPYRIGHT="${APP_COPYRIGHT}"
        APP_DISTRIBUTION="${APP_DISTRIBUTION}"
)

# --- notepadai-editor: helper exe for GIT_SEQUENCE_EDITOR / GIT_EDITOR ---
qt_add_executable(notepadai-editor
    helpers/notepadai-editor.cpp
)
target_link_libraries(notepadai-editor PRIVATE Qt6::Core Qt6::Network)
set_target_properties(notepadai-editor PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
install(TARGETS notepadai-editor RUNTIME DESTINATION bin)
install(TARGETS NotepadAI
    RUNTIME DESTINATION bin
    BUNDLE DESTINATION .
)
