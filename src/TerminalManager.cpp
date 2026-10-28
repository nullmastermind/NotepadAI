/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * Notepad Next is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with Notepad Next.  If not, see <https://www.gnu.org/licenses/>.
 */

#include "TerminalManager.h"

#include "AiAgentDock.h"
#include "ApplicationSettings.h"
#include "DockedEditor.h"
#include "DockWidget.h"
#include "DotEnv.h"
#include "MainWindow.h"
#include "NotepadNextApplication.h"
#include "PreviewTabManager.h"
#include "TerminalAiHelper.h"
#include "TerminalCwdResolver.h"
#include "TerminalDock.h"
#include "TerminalTaskRegistry.h"
#include "TerminalEmptyPanelPolicy.h"
#include "widgets/TerminalEmptyPanel.h"
#include "remote/SshProfile.h"
#include "TerminalWidget.h"
#include "TerminalColorScheme.h"
#include "TerminalAreaToolbar.h"
#include "TerminalIcons.h"
#include "EditorThemeColors.h"
#include "AcpAgentManager.h"
#include "AiDockGroup.h"
#include "DockBottomToolTab.h"

#include "DockAreaWidget.h"
#include "EditorManager.h"
#include "ScintillaNext.h"
#include "DockWidgetTab.h"
#include "DockOverlay.h"
#include "DockManager.h"

#include "iptyprocess.h"
#include "ptyqt.h"

#include "remote/ExecutionContext.h"

#include <QAction>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QIcon>
#include <QMenu>
#include <QMessageBox>
#include <QScopedPointer>
#include <QStandardPaths>
#include <QCoreApplication>
#include <QTimer>
#include <QHash>
#include <QDesktopServices>
#include <QInputDialog>
#include <QUrl>
#include <QSplitter>
#include <QStatusBar>
#include <QMouseEvent>

#include <algorithm>
#include <functional>



TerminalManager::TerminalManager(NotepadNextApplication *app, MainWindow *mainWindow)
    : QObject(mainWindow)
    , m_app(app)
    , m_mainWindow(mainWindow)
{
    m_statsTimer = new QTimer(this);
    m_statsTimer->setInterval(TerminalProcessStats::statsPollMs(false));
    m_statsTimer->setTimerType(Qt::CoarseTimer);
    connect(m_statsTimer, &QTimer::timeout, this, &TerminalManager::refreshProcessStats);
    // Started by updateStatsTimer() once a local terminal is on screen.

    // Editor tabs dropped on a terminal type their path: watch just those tabs.
    if (DockedEditor *editor = mainWindow->getDockedEditor()) {
        for (ScintillaNext *e : mainWindow->editors()) {
            watchEditorTab(e);
        }
        connect(editor, &DockedEditor::editorAdded, this, &TerminalManager::watchEditorTab);
    }

    if (ApplicationSettings *s = app ? app->getSettings() : nullptr) {
        connect(s, &ApplicationSettings::terminalLineHeightPctChanged, this, &TerminalManager::applyAppearance);
        connect(s, &ApplicationSettings::terminalColorSchemeChanged, this, &TerminalManager::applyAppearance);
        connect(s, &ApplicationSettings::terminalMinimumContrastChanged, this, &TerminalManager::applyAppearance);
        connect(s, &ApplicationSettings::terminalCursorShapeChanged, this, &TerminalManager::applyAppearance);
        connect(s, &ApplicationSettings::terminalCursorBlinkChanged, this, &TerminalManager::applyAppearance);
        connect(s, &ApplicationSettings::terminalScrollbackLinesChanged, this, &TerminalManager::applyAppearance);
        connect(s, &ApplicationSettings::terminalCopyOnSelectChanged, this, &TerminalManager::applyAppearance);
        connect(s, &ApplicationSettings::terminalBellSoundChanged, this, &TerminalManager::applyAppearance);
        connect(s, &ApplicationSettings::terminalShowEmptyPanelChanged, this,
                [this](bool) { updateEmptyPanel(); });
    }
}


TerminalManager::~TerminalManager() = default;

QString TerminalManager::resolveShellCommand() const
{
    if (!m_app || !m_app->getSettings()) {
#ifdef Q_OS_WIN
        return qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe"));
#else
        return qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
#endif
    }

    QString configured = m_app->getSettings()->shellCommand();
    if (!configured.isEmpty()) {
        return configured;
    }

#ifdef Q_OS_WIN
    return qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe"));
#else
    return qEnvironmentVariable("SHELL", QStringLiteral("/bin/sh"));
#endif
}

QString TerminalManager::localShell(QString *error) const
{
    // Fail fast if the platform PTY backend is unavailable (e.g. Windows
    // pre-1809 without ConPTY). Probing here avoids creating an empty dock
    // that would otherwise be left attached after the error dialog is dismissed.
    QString problem;
    QString shell;
    {
        QScopedPointer<IPtyProcess> probe(PtyQt::createPtyProcess(IPtyProcess::AutoPty));
        if (!probe || !probe->isAvailable()) {
#ifdef Q_OS_WIN
            problem = tr("Cannot open terminal: ConPTY is unavailable. The embedded terminal requires Windows 10 build 17763 (version 1809) or later.");
#else
            problem = tr("Cannot open terminal: the platform PTY backend is unavailable.");
#endif
        }
    }
    if (problem.isEmpty()) {
        const QString configured = resolveShellCommand();
        shell = configured;
        if (!shell.isEmpty() && !shell.contains(QLatin1Char('/')) && !shell.contains(QLatin1Char('\\'))) {
            const QString resolved = QStandardPaths::findExecutable(shell);
            if (!resolved.isEmpty())
                shell = resolved;
        }
        if (shell.isEmpty() || !QFileInfo::exists(shell)) {
            problem = tr("Cannot launch terminal: shell '%1' was not found. Set Terminal/ShellCommand in Preferences.").arg(configured);
            shell.clear();
        }
    }
    if (!problem.isEmpty()) {
        if (error)
            *error = problem;
        else
            QMessageBox::critical(m_mainWindow, tr("Terminal"), problem);
    }
    return shell;
}

QList<TerminalDock *> TerminalManager::terminalDocks() const
{
    QList<TerminalDock *> out;
    out.reserve(m_docks.size());
    for (const QPointer<TerminalDock> &d : m_docks) {
        if (d)
            out.append(d);
    }
    return out;
}

bool TerminalManager::closeTerminal(TerminalDock *dock, bool force, QString *error)
{
    if (!force && dock->isBusy()) {
        if (error)
            *error = tr("A process is still running in this terminal.");
        return false;
    }
    dock->killProcess();
    ads::CDockWidget *dw = dock->hostDockWidget();
    const bool removed = forgetDock(dock);
    if (dw)
        dw->closeDockWidget();
    else
        dock->deleteLater();
    if (removed)
        applyWorkspaceFilter(m_filterActiveRoot, m_filterOpenRoots);
    return true;
}

TerminalDock *TerminalManager::openTerminal(const QString &cwd, QString *error)
{
    const QString shell = localShell(error);
    if (shell.isEmpty())
        return nullptr;

    auto *dock = new TerminalDock(shell, cwd, m_mainWindow);

    rememberDock(dock);

    configureDock(dock);
    placeInEditor(dock);
    return dock;
}

TerminalDock *TerminalManager::openRemoteTerminal(remote::ExecutionContext *ctx,
                                                  const QString &remoteCwd, const QString &shell)
{
    if (!ctx) {
        return nullptr;
    }
    // No local PTY probe — the backend is an SSH channel on `ctx`. Pass an empty
    // shell through as-is so SshPtyProcess defaults to the remote $SHELL.
    auto *dock = new TerminalDock(ctx, shell, remoteCwd, m_mainWindow);

    rememberDock(dock);

    configureDock(dock);
    placeInEditor(dock);
    return dock;
}

TerminalDock *TerminalManager::openTerminalWithInput(remote::ExecutionContext *ctx,
                                                     const QString &cwd, const QString &text)
{
    TerminalDock *dock = ctx && ctx->isRemote() ? openRemoteTerminal(ctx, cwd)
                                                : openTerminal(cwd);
    if (!dock)
        return nullptr;
    if (TerminalWidget *w = dock->terminalWidget()) {
        const QByteArray bytes = text.toUtf8();
        connect(w, &TerminalWidget::firstOutputReceived, w,
                [w, bytes]() { w->writeToPty(bytes); }, Qt::SingleShotConnection);
    }
    return dock;
}

static QStringList parseEnvText(const QString &envText)
{
    const QList<QPair<QString, QString>> pairs = parseDotEnv(envText);
    QStringList result;
    result.reserve(pairs.size());
    for (const auto &[key, value] : pairs)
        result.append(key + QLatin1Char('=') + value);
    return result;
}

TerminalDock *TerminalManager::openTask(const QString &workspaceCwd, const TerminalTask &task, QString *error)
{
    const QString shell = localShell(error);
    if (shell.isEmpty())
        return nullptr;

    // Resolve cwd: relative → join with workspace, absolute → use directly, empty → workspace
    QString resolvedCwd = workspaceCwd;
    QString cwdWarning;
    if (!task.cwd.isEmpty()) {
        QString taskCwd = task.cwd;
        if (QDir::isRelativePath(taskCwd)) {
            taskCwd = QDir::cleanPath(workspaceCwd + QLatin1Char('/') + taskCwd);
        }
        if (QDir(taskCwd).exists()) {
            resolvedCwd = taskCwd;
        } else {
            cwdWarning = tr("Directory '%1' not found, using workspace root.").arg(task.cwd);
        }
    }

    const QStringList env = parseEnvText(task.env);

    auto *dock = new TerminalDock(shell, resolvedCwd, task.command, task.name, env, m_mainWindow);
    if (!cwdWarning.isEmpty())
        dock->setCwdWarning(cwdWarning);

    rememberDock(dock);

    configureDock(dock);
    placeInEditor(dock);
    return dock;
}

void TerminalManager::openRemoteTask(remote::ExecutionContext *ctx,
                                     const QString &remoteCwd,
                                     const TerminalTask &task)
{
    if (!ctx || !ctx->isRemote()) {
        return;
    }
    if (ctx->state() != remote::ExecutionContext::State::Connected) {
        QMessageBox::warning(
            m_mainWindow,
            tr("Terminal"),
            tr("Cannot run task '%1': remote workspace is not connected.").arg(task.name));
        return;
    }

    auto *dock = new TerminalDock(ctx, remoteCwd, task.command, task.name, m_mainWindow);

    rememberDock(dock);

    configureDock(dock);
    placeInEditor(dock);
}

namespace {

// Double-clicking a terminal tab renames it (ADS would otherwise try to float it).
class TabDoubleClickFilter : public QObject
{
public:
    TabDoubleClickFilter(QObject *parent, std::function<void()> onDoubleClick)
        : QObject(parent)
        , m_onDoubleClick(std::move(onDoubleClick))
    {
    }

protected:
    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (event->type() == QEvent::MouseButtonDblClick
            && static_cast<QMouseEvent *>(event)->button() == Qt::LeftButton) {
            m_onDoubleClick();
            return true;
        }
        return QObject::eventFilter(watched, event);
    }

private:
    std::function<void()> m_onDoubleClick;
};

TerminalCursorShape cursorShapeFromSetting(const QString &value)
{
    if (value == QLatin1String("bar")) return TerminalCursorShape::Bar;
    if (value == QLatin1String("underline")) return TerminalCursorShape::Underline;
    return TerminalCursorShape::Block;
}

TerminalCursorBlink cursorBlinkFromSetting(const QString &value)
{
    if (value == QLatin1String("on")) return TerminalCursorBlink::On;
    if (value == QLatin1String("off")) return TerminalCursorBlink::Off;
    return TerminalCursorBlink::Terminal;
}

} // namespace

TerminalColorScheme TerminalManager::currentScheme() const
{
    const bool dark = m_app ? m_app->isEffectiveThemeDark() : true;
    const QString id = (m_app && m_app->getSettings()) ? m_app->getSettings()->terminalColorScheme() : QString();
    if (TerminalColorScheme::isPreset(id))
        return TerminalColorScheme::forSetting(id, dark);
    const EditorThemeColors editor = EditorThemeColors::forTheme(dark);
    return TerminalColorScheme::followingEditor(dark, editor.background, editor.foreground, editor.selection);
}

QFont TerminalManager::terminalFont() const
{
    QFont f;
    const QString fontStr = (m_app && m_app->getSettings()) ? m_app->getSettings()->terminalFont() : QString();
    if (fontStr.isEmpty() || !f.fromString(fontStr)) {
        f = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    }
    return f;
}

TerminalAppearance TerminalManager::appearance() const
{
    TerminalAppearance a;
    ApplicationSettings *s = m_app ? m_app->getSettings() : nullptr;
    a.scheme = currentScheme();
    a.font = terminalFont();
    if (s) {
        a.lineHeight = std::clamp(s->terminalLineHeightPct(), 100, 200) / 100.0;
        a.minimumContrast = std::clamp(s->terminalMinimumContrast(), 0, 100);
        a.cursorShape = cursorShapeFromSetting(s->terminalCursorShape());
        a.cursorBlink = cursorBlinkFromSetting(s->terminalCursorBlink());
        a.scrollbackLines = std::clamp(s->terminalScrollbackLines(), 0, kMaxScrollbackLines);
        a.copyOnSelect = s->terminalCopyOnSelect();
        a.bellSound = s->terminalBellSound();
    }
    return a;
}

void TerminalManager::applyAppearanceTo(TerminalDock *dock, const TerminalAppearance &appearance)
{
    TerminalWidget *w = dock ? dock->terminalWidget() : nullptr;
    if (!w) return;
    if (dock->isTask()) {
        // Build and test logs are worth keeping in full.
        TerminalAppearance task = appearance;
        task.scrollbackLines = kMaxScrollbackLines;
        w->setAppearance(task);
    } else {
        w->setAppearance(appearance);
    }
}

void TerminalManager::configureDock(TerminalDock *dock)
{
    applyAppearanceTo(dock, appearance());
    wireWidget(dock);
    updateLinkBaseDirs(dock);
}

// Relative paths printed by a terminal resolve against its spawn directory,
// then the workspace that contains it (else the active workspace).
void TerminalManager::updateLinkBaseDirs(TerminalDock *dock)
{
    TerminalWidget *w = dock ? dock->terminalWidget() : nullptr;
    if (!w || dock->isRemoteSession())
        return;
    QStringList dirs{dock->initialCwd()};
    const QString cwd = QDir::cleanPath(dock->initialCwd());
    QString root;
    for (const QString &candidate : m_filterOpenRoots) {
        const QString clean = QDir::cleanPath(candidate);
        if (cwd == clean || cwd.startsWith(clean + QLatin1Char('/'))) {
            root = clean;
            break;
        }
    }
    if (root.isEmpty())
        root = m_filterActiveRoot;
    if (!root.isEmpty() && !dirs.contains(root))
        dirs << root;
    w->setLinkBaseDirs(dirs);
}

void TerminalManager::applyAppearance()
{
    applyEmptyPanelColors();
    const TerminalAppearance a = appearance();
    for (const auto &d : m_docks) {
        if (!d.isNull()) {
            applyAppearanceTo(d.data(), a);
            updateTab(d.data());
        }
    }
}

void TerminalManager::applyTheme()
{
    applyAppearance();
}

void TerminalManager::applyFont()
{
    applyAppearance();
}

void TerminalManager::zoomTerminals(int steps)
{
    if (!m_app || !m_app->getSettings()) return;
    QFont f = terminalFont();
    const qreal size = steps == 0 ? qreal(kDefaultTerminalPointSize)
                                  : std::clamp(f.pointSizeF() + steps, 6.0, 48.0);
    if (qFuzzyCompare(size, f.pointSizeF())) return;
    f.setPointSizeF(size);
    // Persisted; terminalFontChanged re-applies it to every terminal.
    m_app->getSettings()->setTerminalFont(f.toString());
}

void TerminalManager::shutdown()
{
    m_shuttingDown = true;
    hideEmptyPanel();
    if (m_statsTimer)
        m_statsTimer->stop();

    // killProcess() routes to TerminateProcess (Windows) / SIGKILL (POSIX)
    // which mark the child for teardown synchronously at the kernel level.
    // The app is exiting — no downstream operation needs the child actually
    // reaped, and QPointer guards in m_docks survive a late destroyed signal.
    for (auto &d : m_docks) {
        if (d.isNull()) continue;
        if (auto *w = d->terminalWidget()) {
            w->killProcess();
        }
    }
}

void TerminalManager::refreshProcessStats()
{
    if (m_shuttingDown)
        return;

    // Only terminals on screen are sampled; a hidden tab's numbers are not read.
    bool anyLive = false;
    for (const auto &d : m_docks) {
        if (d.isNull() || d->isRemoteSession() || !d->isVisible())
            continue;
        TerminalWidget *w = d->terminalWidget();
        if (w && w->ptyPid() > 0) {
            anyLive = true;
            break;
        }
    }

    if (!anyLive) {
        for (const auto &d : m_docks) {
            if (d.isNull() || d->isRemoteSession())
                continue;
            d->setProcessStats({});
            d->setCoreLoad({});
        }
        m_treeCpu.clear();
        m_processorPrev.clear();
        m_processorPct.clear();
        return;
    }

    QString coreLoad;
    if (TerminalProcessStats::enumerateProcessorTicks(m_processorNow)) {
        if (m_processorPct.size() != m_processorNow.size())
            m_processorPct.resize(m_processorNow.size());
        const bool sameSpan = m_processorPrev.size() == m_processorNow.size();
        TerminalProcessStats::processorLoad(
            m_processorNow.constData(), m_processorNow.size(),
            sameSpan ? m_processorPrev.constData() : nullptr,
            m_processorPct.data());
        if (sameSpan) {
            coreLoad = TerminalProcessStats::coreLoadLabel(
                m_processorPct.constData(), m_processorPct.size());
        }
        m_processorPrev = m_processorNow;
    } else {
        m_processorPrev.clear();
    }

    if (!TerminalProcessStats::enumerateProcesses(m_processSnapshot)) {
        for (const auto &d : m_docks) {
            if (d.isNull() || d->isRemoteSession())
                continue;
            d->setCoreLoad(coreLoad);
        }
        return;
    }

    const quint64 now = TerminalProcessStats::nowWall100ns();
    const int cpus = TerminalProcessStats::logicalCpuCount();
    QHash<quint32, TerminalProcessStats::TreeCpuState> nextState;
    nextState.reserve(m_docks.size());

    const TerminalProcessStats::ProcessSample *samples = m_processSnapshot.constData();
    const int n = m_processSnapshot.size();

    for (const auto &d : m_docks) {
        if (d.isNull() || d->isRemoteSession() || !d->isVisible())
            continue;
        TerminalWidget *w = d->terminalWidget();
        const qint64 pid64 = w ? w->ptyPid() : 0;
        if (pid64 <= 0 || pid64 > static_cast<qint64>(0xffffffffu)) {
            d->setProcessStats({});
            d->setCoreLoad({});
            continue;
        }
        const quint32 pid = static_cast<quint32>(pid64);
        const auto it = m_treeCpu.constFind(pid);
        const TerminalProcessStats::TreeCpuState *prev =
            (it == m_treeCpu.cend()) ? nullptr : &it.value();
        TerminalProcessStats::TreeCpuState out{};
        const TerminalProcessStats::TreeStats stats = TerminalProcessStats::aggregateTree(
            samples, n, pid, prev, now, cpus, &out);
        d->setStats(stats, coreLoad);
        if (stats.valid)
            nextState.insert(pid, out);
    }
    m_treeCpu.swap(nextState);
}



TerminalDock *TerminalManager::findTaskDock(const QString &command, const QString &cwd) const
{
    const QString cleanCwd = QDir::cleanPath(cwd);
    for (const auto &d : m_docks) {
        if (d.isNull()) continue;
        if (!d->isTask()) continue;
        if (d->taskCommand() == command &&
            QDir::cleanPath(d->initialCwd()).compare(cleanCwd, Qt::CaseInsensitive) == 0) {
            return d.data();
        }
    }
    return nullptr;
}

void TerminalManager::runOrRestartTask(const QString &cwd, const TerminalTask &task,
                                       remote::ExecutionContext *context)
{
    TerminalDock *existing = findTaskDock(task.command, cwd);
    if (existing) {
        existing->reveal();
        existing->restartTask();
        if (auto *w = existing->terminalWidget()) {
            w->setFocus();
        }
        return;
    }
    if (context && context->isRemote()) {
        openRemoteTask(context, cwd, task);
    } else {
        openTask(cwd, task);
    }
}

QList<TerminalTask> TerminalManager::tasksForWorkspace(const QString &workspacePath) const
{
    if (!m_app || !m_app->getSettings())
        return {};
    return TerminalTaskRegistry(m_app->getSettings()).tasksForWorkspace(workspacePath);
}

void TerminalManager::addTask(const QString &workspacePath, const TerminalTask &task)
{
    if (!m_app || !m_app->getSettings())
        return;
    TerminalTaskRegistry(m_app->getSettings()).addTask(workspacePath, task);
}

void TerminalManager::setTasks(const QString &workspacePath, const QList<TerminalTask> &tasks)
{
    if (!m_app || !m_app->getSettings())
        return;
    TerminalTaskRegistry(m_app->getSettings()).setTasks(workspacePath, tasks);
}

void TerminalManager::placeInEditor(TerminalDock *dock)
{
    DockedEditor *editor = m_mainWindow->getDockedEditor();
    if (!editor)
        return;
    ads::CDockWidget *dw = editor->addBottomToolTab(dock, dock->windowTitle(), QIcon());
    if (!dw)
        return;


    connect(dock, &QWidget::windowTitleChanged, dw, [dw](const QString &title) {
        dw->setWindowTitle(title);
    });
    // The tab tooltip carries cwd/shell/PID, not just the title.
    disconnect(dw, &QWidget::windowTitleChanged, dw->tabWidget(), &QWidget::setToolTip);
    connect(dock, &TerminalDock::presentationChanged, this, [this, dock = QPointer<TerminalDock>(dock)]() {
        if (dock)
            updateTab(dock.data());
    });
    updateTab(dock);
    dw->tabWidget()->installEventFilter(new TabDoubleClickFilter(dw->tabWidget(), [this, dock = QPointer<TerminalDock>(dock)]() {
        if (dock)
            renameTerminal(dock.data());
    }));
    // A tab can be dragged into another area; give that area the controls.
    connect(dw, &ads::CDockWidget::visibilityChanged, this, [this, dw = QPointer<ads::CDockWidget>(dw)](bool visible) {
        if (visible && dw)
            installToolbar(dw->dockAreaWidget());
        updateStatsTimer();
    });

    dw->setFeature(ads::CDockWidget::DockWidgetFeature::CustomCloseHandling, true);
    connect(dw, &ads::CDockWidget::closeRequested, this,
            [this, dock = QPointer<TerminalDock>(dock),
             dw = QPointer<ads::CDockWidget>(dw)]() {
        if (dock.isNull())
            return;
        if (!dock->confirmClose())
            return;
        // confirmClose runs a nested event loop; workspace close may have
        // already forgotten and queued delete on this tab.
        if (dock.isNull())
            return;
        const bool removed = forgetDock(dock.data());
        if (!dw.isNull())
            dw->closeDockWidget();
        else if (!dock.isNull())
            dock->deleteLater();
        if (removed)
            applyWorkspaceFilter(m_filterActiveRoot, m_filterOpenRoots);
    });

    // Explicitly creating a terminal re-arms the empty panel for its
    // workspace: if the user closed the panel earlier in this workspace's
    // lifetime, closing this terminal brings the panel back.
    {
        const auto space = dock->isRemoteSession()
                               ? TerminalCwdResolver::CwdSpace::Remote
                               : TerminalCwdResolver::CwdSpace::Local;
        const QString owner = TerminalCwdResolver::matchingWorkspace(
            dock->initialCwd(), space, m_filterOpenRoots);
        if (!owner.isEmpty())
            m_emptySuppressed.remove(TerminalEmptyPanelPolicy::workspaceKey(owner));
    }

    dw->toggleView(true);
    dw->raise();
    installToolbar(dw->dockAreaWidget());
    if (auto *tw = dock->terminalWidget())
        tw->setFocus();
    applyWorkspaceFilter(m_filterActiveRoot, m_filterOpenRoots);
    updateStatsTimer();
}

void TerminalManager::installToolbar(ads::CDockAreaWidget *area)
{
    if (!area)
        return;
    if (TerminalAreaToolbar *existing = TerminalAreaToolbar::find(area)) {
        existing->syncToCurrent();
        return;
    }
    m_toolbars.removeIf([](const QPointer<TerminalAreaToolbar> &tb) { return tb.isNull(); });
    auto *toolbar = new TerminalAreaToolbar(area);
    m_toolbars.append(toolbar);
    if (QSplitter *splitter = toolAreaSplitter(area)) {
        // A manual drag ends "maximized"; keep the button honest.
        connect(splitter, &QSplitter::splitterMoved, toolbar, [toolbar]() {
            toolbar->setMaximized(isToolAreaMaximized(toolbar->area()));
        });
    }
    connect(toolbar, &TerminalAreaToolbar::findRequested, this, [](TerminalDock *dock) {
        if (TerminalWidget *w = dock->terminalWidget())
            w->openSearch();
    });
    connect(toolbar, &TerminalAreaToolbar::clearRequested, this, [](TerminalDock *dock) {
        dock->clearTerminal();
    });
    connect(toolbar, &TerminalAreaToolbar::restartRequested, this, [](TerminalDock *dock) {
        dock->restartTask();
    });
    connect(toolbar, &TerminalAreaToolbar::newTerminalRequested, this, &TerminalManager::openTerminalLike);
    connect(toolbar, &TerminalAreaToolbar::maximizeRequested, this, [](TerminalAreaToolbar *tb) {
        tb->setMaximized(toggleToolAreaMaximized(tb->area()));
    });
    connect(toolbar, &TerminalAreaToolbar::cpuHoverChanged, this, [this]() {
        if (syncStatsPoll())
            refreshProcessStats();
    });
    connect(toolbar, &TerminalAreaToolbar::currentDockChanged, this, [this]() {
        updateStatsTimer();
    });
}

void TerminalManager::openTerminalLike(TerminalDock *like)
{
    if (like && like->isRemoteSession()) {
        if (remote::ExecutionContext *ctx = like->executionContext()) {
            openRemoteTerminal(ctx, like->currentCwd(), QString());
            return;
        }
    }
    QString cwd = like ? like->currentCwd() : QString();
    if (cwd.isEmpty() || !QFileInfo(cwd).isDir())
        cwd = QDir::homePath();
    openTerminal(cwd);
}

void TerminalManager::renameTerminal(TerminalDock *dock)
{
    if (!dock || dock->isTask())
        return;
    // Guard taken before the dialog: its nested event loop can delete the tab.
    const QPointer<TerminalDock> guard(dock);
    bool ok = false;
    const QString current = dock->customName().isEmpty() ? dock->windowTitle() : dock->customName();
    const QString name = QInputDialog::getText(m_mainWindow, tr("Rename Terminal"),
                                               tr("Name (empty restores the automatic title):"),
                                               QLineEdit::Normal, current, &ok);
    if (ok && guard)
        guard->setCustomName(name);
}

QIcon TerminalManager::tabIcon(const TerminalDock *dock) const
{
    const QPalette pal = m_mainWindow->palette();
    // Status colours are the active scheme's own green and red, so they fit
    // the theme instead of being fixed hex values.
    const TerminalColorScheme scheme = currentScheme();
    QString svg = QStringLiteral(":/icons/square-terminal.svg");
    QColor color = pal.color(QPalette::PlaceholderText);
    switch (dock->taskState()) {
    case TerminalDock::TaskState::Running:
        svg = QStringLiteral(":/icons/play.svg");
        color = pal.color(QPalette::ButtonText);
        break;
    case TerminalDock::TaskState::Succeeded:
        svg = QStringLiteral(":/icons/check.svg");
        color = scheme.ansi[2];
        break;
    case TerminalDock::TaskState::Failed:
        svg = QStringLiteral(":/icons/circle-x.svg");
        color = scheme.ansi[1];
        break;
    case TerminalDock::TaskState::None:
        if (dock->isTask())
            svg = QStringLiteral(":/icons/play.svg");
        break;
    }
    const QColor dot = dock->bellPending() ? pal.color(QPalette::Highlight) : QColor();
    return terminalTintedIcon(svg, color, dot);
}

void TerminalManager::updateTab(TerminalDock *dock)
{
    ads::CDockWidget *dw = dock->hostDockWidget();
    if (!dw)
        return;
    dw->setIcon(tabIcon(dock));
    dw->setTabToolTip(dock->tabToolTip());
}

void TerminalManager::wireContextMenu(TerminalDock *dock)
{
    TerminalWidget *tw = dock->terminalWidget();
    connect(tw, &TerminalWidget::contextMenuAboutToShow,
            this, [this, dock = QPointer<TerminalDock>(dock)](QMenu *menu) {
        if (!dock)
            return;
        TerminalWidget *w = dock->terminalWidget();
        // Always shown, usable with a selection; the target AI tab is found
        // (or started) when it is used.
        QAction *sendAi = menu->addAction(tr("Send to AI"));
        sendAi->setShortcut(QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_A));
        sendAi->setEnabled(w && w->hasSelection());
        connect(sendAi, &QAction::triggered, this, [this, dock]() {
            if (dock)
                sendSelectionToAi(dock.data());
        });
    });
    connect(tw, &TerminalWidget::contextMenuTailAboutToShow,
            this, [this, dock = QPointer<TerminalDock>(dock)](QMenu *menu) {
        if (!dock)
            return;
        menu->addSeparator();
        if (!dock->isTask()) {
            connect(menu->addAction(tr("Rename...")), &QAction::triggered, this, [this, dock]() {
                if (dock)
                    renameTerminal(dock.data());
            });
        }
        connect(menu->addAction(tr("New Terminal")), &QAction::triggered, this, [this, dock]() {
            openTerminalLike(dock.data());
        });
        connect(menu->addAction(tr("Close")), &QAction::triggered, this, [dock]() {
            if (!dock)
                return;
            // Same path as the tab's close button (confirmation included),
            // after the menu's exec() returns: closing deletes the widget
            // whose contextMenuEvent is still on the stack.
            if (ads::CDockWidget *dw = dock->hostDockWidget())
                QTimer::singleShot(0, dw, [dw]() { dw->requestCloseDockWidget(); });
        });
    });
}

void TerminalManager::wireWidget(TerminalDock *dock)
{
    TerminalWidget *tw = dock->terminalWidget();
    if (!tw) return;
    wireContextMenu(dock);
    connect(tw, &TerminalWidget::zoomRequested, this, &TerminalManager::zoomTerminals);
    connect(tw, &TerminalWidget::openFileRequested, this, &TerminalManager::openFileAt);
    connect(tw, &TerminalWidget::revealPathRequested, this, &TerminalManager::revealPath);
    connect(tw, &TerminalWidget::sendToAiRequested, this, [this, dock = QPointer<TerminalDock>(dock)]() {
        if (dock)
            sendSelectionToAi(dock.data());
    });
}

void TerminalManager::revealPath(const QString &path)
{
    if (!m_mainWindow->revealInWorkspace(path))
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
}

TerminalDock *TerminalManager::terminalAt(const QPoint &globalPos) const
{
    for (const auto &d : m_docks) {
        TerminalWidget *w = d ? d->terminalWidget() : nullptr;
        if (!w || !w->isVisible())
            continue;
        QWidget *vp = w->viewport();
        if (vp->rect().contains(vp->mapFromGlobal(globalPos)))
            return d.data();
    }
    return nullptr;
}

void TerminalManager::watchEditorTab(ScintillaNext *editor)
{
    for (QWidget *w = editor; w; w = w->parentWidget()) {
        if (auto *dw = qobject_cast<ads::CDockWidget *>(w)) {
            dw->tabWidget()->installEventFilter(this);
            return;
        }
    }
}

bool TerminalManager::eventFilter(QObject *watched, QEvent *event)
{
    // ADS drags tabs itself (no MIME drag), but the tab still receives the
    // release before ADS finishes the drag, so a file tab let go over a
    // terminal can type its path there. Only editor tabs are watched.
    if (event->type() != QEvent::MouseButtonRelease)
        return QObject::eventFilter(watched, event);
    auto *tab = qobject_cast<ads::CDockWidgetTab *>(watched);
    if (!tab)
        return QObject::eventFilter(watched, event);
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() != Qt::LeftButton)
        return QObject::eventFilter(watched, event);
    ads::CDockWidget *dw = tab->dockWidget();
    auto *editor = dw ? qobject_cast<ScintillaNext *>(dw->widget()) : nullptr;
    if (!editor || !editor->isFile())
        return QObject::eventFilter(watched, event);
    const QString path = editor->getFilePath();
    if (path.startsWith(QLatin1String("ssh://")))
        return QObject::eventFilter(watched, event);

    const QPoint global = me->globalPosition().toPoint();
    TerminalDock *target = terminalAt(global);
    // A local path means nothing to a remote shell.
    if (!target || target->isRemoteSession())
        return QObject::eventFilter(watched, event);
    // When ADS offers a dock target under the pointer the user is docking.
    if (ads::CDockManager *manager = dw->dockManager()) {
        const auto overlays = manager->findChildren<ads::CDockOverlay *>();
        for (ads::CDockOverlay *overlay : overlays) {
            if (overlay->isVisible() && overlay->visibleDropAreaUnderCursor() != ads::InvalidDockWidgetArea)
                return QObject::eventFilter(watched, event);
        }
    }
    target->terminalWidget()->pasteDroppedPaths({path});
    return QObject::eventFilter(watched, event);
}

void TerminalManager::openFileAt(const QString &path, int line, int column)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    // file:line links (compiler output) mean text even for an SVG; a bare path
    // to an asset opens its preview and leaves any text editor of it alone.
    if (line > 0) {
        m_mainWindow->openFileAsText(absolute);
    } else {
        const QString typeId = m_mainWindow->assetPreviewTypeFor(absolute);
        if (!typeId.isEmpty() && m_app && m_app->getPreviewTabManager()
            && m_app->getPreviewTabManager()->openFilePreview(absolute, typeId, PreviewTabManager::OpenMode::Pinned,
                                                              true))
            return;
        m_mainWindow->openFileAsText(absolute);
    }
    ScintillaNext *editor = (m_app && m_app->getEditorManager())
        ? m_app->getEditorManager()->getEditorByFilePath(absolute) : nullptr;
    if (!editor)
        return;
    if (line > 0) {
        const sptr_t lineIndex = line - 1;
        const sptr_t pos = column > 0 ? editor->findColumn(lineIndex, column - 1)
                                      : editor->positionFromLine(lineIndex);
        editor->ensureVisible(lineIndex);
        editor->gotoPos(pos);
        editor->verticalCentreCaret();
    }
    m_mainWindow->getDockedEditor()->switchToEditor(editor);
    static_cast<QWidget *>(editor)->setFocus(Qt::OtherFocusReason);
}

// The AI tab of the terminal's own project: the aiDockGroupKey (directory +
// SSH profile) of the terminal's cwd or its nearest ancestor that has one,
// else the active AI tab.
AiAgentDock *TerminalManager::aiDockFor(TerminalDock *dock) const
{
    AcpAgentManager *agents = m_app ? m_app->getAiAgentManager() : nullptr;
    if (agents) {
        const remote::ExecutionContext *ctx = dock->executionContext();
        const QString profile = (ctx && ctx->isRemote()) ? ctx->sshProfileId() : QString();
        QString dir = QDir::cleanPath(dock->currentCwd());
        while (!dir.isEmpty()) {
            if (AiAgentDock *found = agents->dockForGroup(aiDockGroupKey(dir, profile)))
                return found;
            const qsizetype slash = dir.lastIndexOf(QLatin1Char('/'));
            if (slash < 0 || dir == QLatin1String("/") || dir.endsWith(QLatin1String(":/")))
                break;
            // "/home" -> "/" so the POSIX root is tried too.
            dir = slash == 0 ? QStringLiteral("/") : dir.left(slash);
            // "C:" -> "C:/" so the drive root is tried too.
            if (dir.size() == 2 && dir.at(1) == QLatin1Char(':'))
                dir += QLatin1Char('/');
        }
    }
    return m_mainWindow->activeAiDock();
}

void TerminalManager::sendSelectionToAi(TerminalDock *dock)
{
    TerminalWidget *tw = dock ? dock->terminalWidget() : nullptr;
    if (!tw || !tw->hasSelection())
        return;
    const QString wrapped = wrapInCodeblock(tw->selectedText());
    AiAgentDock *target = aiDockFor(dock);
    if (!target) {
        // No AI tab yet: start one in the terminal's directory.
        target = m_mainWindow->openAiAgentInFolder(dock->currentCwd(), dock->executionContext());
    }
    if (!target) {
        m_mainWindow->statusBar()->showMessage(tr("No AI agent could be started for this folder"), 4000);
        return;
    }
    target->insertTextToInput(wrapped);
    target->setVisible(true);
    target->raise();
}

void TerminalManager::rememberDock(TerminalDock *dock)
{
    m_docks.append(QPointer<TerminalDock>(dock));
    // Restarting a task swaps in a fresh widget; give it the same setup.
    connect(dock, &TerminalDock::terminalRecreated, this, [this, dock]() {
        configureDock(dock);
    });
    connect(dock, &QObject::destroyed, this, [this](QObject *obj) {
        if (forgetDock(obj))
            applyWorkspaceFilter(m_filterActiveRoot, m_filterOpenRoots);
        syncStatsPoll();
        updateStatsTimer();
    });
}

void TerminalManager::updateStatsTimer()
{
    if (!m_statsTimer || m_shuttingDown)
        return;
    bool any = false;
    for (const auto &d : m_docks) {
        if (d && !d->isRemoteSession() && d->isVisible() && d->terminalWidget()
            && d->terminalWidget()->ptyPid() > 0) {
            any = true;
            break;
        }
    }
    if (any && !m_statsTimer->isActive()) {
        m_statsTimer->start();
        refreshProcessStats();
    } else if (!any && m_statsTimer->isActive()) {
        m_statsTimer->stop();
        m_treeCpu.clear();
        m_processorPrev.clear();
    }
}

bool TerminalManager::syncStatsPoll()
{
    bool open = false;
    for (const auto &tb : m_toolbars) {
        if (tb && tb->cpuLabelUnderMouse()) {
            open = true;
            break;
        }
    }
    if (m_statsTimer) {
        const int ms = TerminalProcessStats::statsPollMs(open);
        if (m_statsTimer->interval() != ms) {
            m_statsTimer->setInterval(ms);
            if (open)
                m_statsTimer->start();
        }
    }
    return open;
}

bool TerminalManager::forgetDock(QObject *obj)
{
    if (!obj)
        return false;
    bool removed = false;
    for (int i = m_docks.size() - 1; i >= 0; --i) {
        if (m_docks[i].isNull() || m_docks[i].data() == obj) {
            m_docks.removeAt(i);
            removed = true;
        }
    }
    return removed;
}

void TerminalManager::applyWorkspaceFilter(const QString &activeWorkspaceRoot,
                                           const QStringList &openWorkspaceRoots)
{
    if (m_shuttingDown || QCoreApplication::closingDown())
        return;
    m_filterActiveRoot = activeWorkspaceRoot;
    m_filterOpenRoots = openWorkspaceRoots;
    for (const auto &d : m_docks) {
        if (d)
            updateLinkBaseDirs(d.data());
    }


    // Two linear passes over live docks. No cwd/space/index scratch lists —
    // ADS toggleView/layout dominates. N is the open terminal count (tens).
    bool anyUnmatched = activeWorkspaceRoot.isEmpty() || openWorkspaceRoots.isEmpty();
    if (!anyUnmatched) {
        for (const auto &d : m_docks) {
            if (d.isNull())
                continue;
            const auto space = d->isRemoteSession()
                                   ? TerminalCwdResolver::CwdSpace::Remote
                                   : TerminalCwdResolver::CwdSpace::Local;
            if (TerminalCwdResolver::matchingWorkspace(d->initialCwd(), space, m_filterOpenRoots)
                    .isEmpty()) {
                anyUnmatched = true;
                break;
            }
        }
    }

    for (const auto &d : m_docks) {
        if (d.isNull())
            continue;
        ads::CDockWidget *dw = d->hostDockWidget();
        if (!dw)
            continue;
        const auto space = d->isRemoteSession()
                               ? TerminalCwdResolver::CwdSpace::Remote
                               : TerminalCwdResolver::CwdSpace::Local;
        const bool want = TerminalCwdResolver::terminalTabWanted(
            d->initialCwd(), space, m_filterOpenRoots, m_filterActiveRoot, anyUnmatched);
        if (want) {
            if (dw->isClosed())
                dw->toggleView(true);
        } else if (!dw->isClosed()) {
            dw->toggleView(false);
        }
    }
    updateEmptyPanel();
}


void TerminalManager::closeTerminalsForWorkspace(const QString &workspaceRoot,
                                                 const QStringList &openWorkspaceRoots)
{
    if (workspaceRoot.isEmpty())
        return;

    // The workspace is going away: its "panel closed" suppression dies with
    // it, so re-opening the workspace shows the panel again.
    const QString emptyKey = TerminalEmptyPanelPolicy::workspaceKey(workspaceRoot);
    m_emptySuppressed.remove(emptyKey);
    m_emptyPanelDirty = true;

    QStringList cwds;
    QList<TerminalCwdResolver::CwdSpace> spaces;
    QList<QPointer<TerminalDock>> live;
    const int n = m_docks.size();
    cwds.reserve(n);
    spaces.reserve(n);
    live.reserve(n);
    for (const auto &d : m_docks) {
        if (d.isNull())
            continue;
        live.append(d);
        cwds.append(d->initialCwd());
        spaces.append(d->isRemoteSession()
                          ? TerminalCwdResolver::CwdSpace::Remote
                          : TerminalCwdResolver::CwdSpace::Local);
    }

    const QList<int> idxs = TerminalCwdResolver::closeTerminalIndices(
        cwds, spaces, openWorkspaceRoots, workspaceRoot);
    QList<QPointer<TerminalDock>> toClose;
    toClose.reserve(idxs.size());
    for (int idx : idxs) {
        if (idx >= 0 && idx < live.size() && !live.at(idx).isNull())
            toClose.append(live.at(idx));
    }

    for (const QPointer<TerminalDock> &dock : toClose)
        forgetDock(dock.data());

    for (const QPointer<TerminalDock> &dock : toClose) {
        if (dock.isNull())
            continue;
        if (auto *w = dock->terminalWidget())
            w->killProcess();
        if (dock.isNull())
            continue;
        if (ads::CDockWidget *dw = dock->hostDockWidget())
            dw->closeDockWidget();
        else
            dock->deleteLater();
    }
}



// --- Terminal empty panel -------------------------------------------------

void TerminalManager::applyEmptyPanelColors()
{
    if (!m_emptyPanel)
        return;
    const TerminalColorScheme scheme = currentScheme();
    m_emptyPanel->setSchemeColors(scheme.defaultBg, scheme.defaultFg, scheme.selection);
    if (m_emptyDock) {
        const QColor tint = m_mainWindow->palette().color(QPalette::PlaceholderText);
        m_emptyDock->setIcon(
            terminalTintedIcon(QStringLiteral(":/icons/square-terminal.svg"), tint));
    }
}

void TerminalManager::hideEmptyPanel()
{
    if (m_emptyDock && !m_emptyDock->isClosed())
        m_emptyDock->toggleView(false);
}

void TerminalManager::refreshEmptyPanel()
{
    m_emptyPanelDirty = true;
    updateEmptyPanel();
}

void TerminalManager::ensureEmptyPanel()
{
    if (m_emptyPanel)
        return;
    DockedEditor *editor = m_mainWindow->getDockedEditor();
    if (!editor)
        return;

    m_emptyPanel = new TerminalEmptyPanel(m_mainWindow);
    applyEmptyPanelColors();

    connect(m_emptyPanel, &TerminalEmptyPanel::newTerminalRequested, this, [this]() {
        const QString root = m_filterActiveRoot;
        remote::ExecutionContext *ctx = m_mainWindow->activeExecutionContext();
        if (ctx && ctx->isRemote()) {
            const QString cwd = TerminalCwdResolver::resolveWorkspaceForContext(ctx, root);
            if (!cwd.isEmpty())
                openRemoteTerminal(ctx, cwd, QString());
            return;
        }
        const QString cwd = TerminalCwdResolver::resolveWorkspace(root);
        if (!cwd.isEmpty())
            openTerminal(cwd);
    });
    connect(m_emptyPanel, &TerminalEmptyPanel::configuredTaskRequested, this,
            [this](const TerminalTask &task) {
        const QString cwd = TerminalCwdResolver::resolveWorkspace(m_filterActiveRoot);
        if (!cwd.isEmpty())
            openTask(cwd, task);
    });
    connect(m_emptyPanel, &TerminalEmptyPanel::editTasksRequested, this,
            &TerminalManager::editTasksRequested);

    const QColor tint = m_mainWindow->palette().color(QPalette::PlaceholderText);
    ads::CDockWidget *dw = editor->addBottomToolTab(
        m_emptyPanel, tr("Terminal"),
        terminalTintedIcon(QStringLiteral(":/icons/square-terminal.svg"), tint));
    if (!dw) {
        m_emptyPanel->deleteLater();
        m_emptyPanel = nullptr;
        return;
    }
    m_emptyDock = dw;

    // Closing the panel tab (the only X the ADS area offers) hides it and
    // suppresses it for this workspace's lifetime; nothing is destroyed, so
    // another workspace can reuse the same panel.
    dw->setFeature(ads::CDockWidget::DockWidgetFeature::CustomCloseHandling, true);
    connect(dw, &ads::CDockWidget::closeRequested, this,
            [this, dw = QPointer<ads::CDockWidget>(dw)]() {
        if (!m_emptyPanelKey.isEmpty())
            m_emptySuppressed.insert(m_emptyPanelKey);
        if (dw && !dw->isClosed())
            dw->toggleView(false);
    });
    dw->toggleView(false);
}

void TerminalManager::updateEmptyPanel()
{
    if (QCoreApplication::closingDown())
        return;

    TerminalEmptyPanelPolicy::Input in;
    in.settingEnabled = m_app && m_app->getSettings()
                            ? m_app->getSettings()->terminalShowEmptyPanel()
                            : false;
    in.hasActiveWorkspace = !m_filterActiveRoot.isEmpty();
    const QString key = TerminalEmptyPanelPolicy::workspaceKey(m_filterActiveRoot);
    in.suppressedForActiveWorkspace = !key.isEmpty() && m_emptySuppressed.contains(key);
    in.shuttingDown = m_shuttingDown;
    for (const auto &d : m_docks) {
        if (d.isNull())
            continue;
        if (ads::CDockWidget *dw = d->hostDockWidget(); dw && !dw->isClosed()) {
            in.anyTerminalVisible = true;
            break;
        }
    }

    if (!TerminalEmptyPanelPolicy::shouldShow(in)) {
        hideEmptyPanel();
        return;
    }

    ensureEmptyPanel();
    if (!m_emptyPanel || !m_emptyDock)
        return;

    // Refresh on every (re)show and on workspace switch, so the configured
    // tasks are fresh after edits elsewhere.
    if (m_emptyDock->isClosed())
        m_emptyPanelDirty = true;
    if (m_emptyPanelDirty || m_emptyPanelKey != key) {
        const bool remoteWs = remote::isSshUri(m_filterActiveRoot);
        const remote::SshUri ssh = remote::parseSshUri(m_filterActiveRoot);
        m_emptyPanel->setContent(
            remoteWs && ssh.valid ? ssh.remotePath : m_filterActiveRoot, remoteWs,
            remoteWs ? QList<TerminalTask>() : tasksForWorkspace(m_filterActiveRoot));
        m_emptyPanelKey = key;
        m_emptyPanelDirty = false;
    }

    if (m_emptyDock->isClosed()) {
        m_emptyDock->toggleView(true);
        m_emptyDock->raise();
    }
}
