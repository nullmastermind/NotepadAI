/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// preview.*, browser.*, miniapps.* (docs/adr/0007).

#include "Registrar.h"

#include "DockedEditor.h"
#include "MiniAppInstance.h"
#include "MiniAppManager.h"
#include "MiniAppRegistry.h"
#include "NotepadNextApplication.h"
#include "PreviewTabManager.h"
#include "QuickBrowseUrl.h"
#include "ScintillaNext.h"
#include "dialogs/MainWindow.h"
#include "widgets/WebViewWidget.h"

#include "DockWidget.h"

#include <QJsonArray>
#include <QMetaEnum>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QPointer>
#include <QSet>
#include <QTimer>
#include <QUrl>
#include <QUuid>

#include <algorithm>

namespace control::commands {

namespace {

// --- Mini app definitions (EditMiniAppsDialog's job) -------------------------

const QStringList &proxyTypes()
{
    // MiniAppDefinition::proxyType: 0 none, 1 http, 2 https, 3 socks4, 4 socks5.
    static const QStringList types{QStringLiteral("none"), QStringLiteral("http"), QStringLiteral("https"),
                                   QStringLiteral("socks4"), QStringLiteral("socks5")};
    return types;
}

// The fields add and update take (update: all optional).
QList<Arg> miniAppFieldArgs(bool forAdd)
{
    auto field = [forAdd](const QString &name, ArgType type, const QString &description, bool requiredOnAdd = false) {
        return forAdd && requiredOnAdd ? Registrar::required(name, type, description)
                                       : Registrar::optional(name, type, description);
    };
    Arg env = Registrar::optional(QStringLiteral("env"), ArgType::Any,
                                  QStringLiteral("environment for `command`: KEY=VALUE lines, or an object of strings "
                                                 "(replaces the whole env; \"\" clears it)"));
    env.secret = true;
    return {field(QStringLiteral("name"), ArgType::String, QStringLiteral("display name"), true),
            field(QStringLiteral("url"), ArgType::String, QStringLiteral("http:// or https:// page the tab opens"), true),
            field(QStringLiteral("group"), ArgType::String, QStringLiteral("menu group under Browser > Mini Apps (\"\" for none)")),
            field(QStringLiteral("command"), ArgType::String,
                  QStringLiteral("shell command started first (a dev server); the page opens once it answers")),
            env,
            field(QStringLiteral("cwd"), ArgType::String, QStringLiteral("working directory of `command`")),
            field(QStringLiteral("healthCheckUrl"), ArgType::String,
                  QStringLiteral("URL polled until it answers (default: `url`); turns the advanced options on")),
            field(QStringLiteral("healthTimeoutMs"), ArgType::Int,
                  QStringLiteral("how long to poll, 5000-300000 ms (default 60000); turns the advanced options on")),
            field(QStringLiteral("advanced"), ArgType::Bool,
                  QStringLiteral("use healthCheckUrl/healthTimeoutMs (off keeps them but launches with the defaults)")),
            field(QStringLiteral("debugPort"), ArgType::Int,
                  QStringLiteral("Chrome DevTools Protocol port, 1-65535 (0: none); turns debugging on")),
            field(QStringLiteral("debug"), ArgType::Bool, QStringLiteral("open the debug port (off keeps the port)")),
            field(QStringLiteral("autoKillOnClose"), ArgType::Bool,
                  QStringLiteral("stop `command` when the tab closes (default true)")),
            field(QStringLiteral("allowCrossOrigin"), ArgType::Bool, QStringLiteral("let the page make cross-origin requests")),
            Registrar::oneOf(QStringLiteral("proxy"), proxyTypes(), QStringLiteral("proxy for the page; none turns it off")),
            field(QStringLiteral("proxyHost"), ArgType::String, QStringLiteral("proxy host")),
            field(QStringLiteral("proxyPort"), ArgType::Int, QStringLiteral("proxy port (0: the scheme's default)")),
            field(QStringLiteral("proxyBypass"), ArgType::String, QStringLiteral("hosts that skip the proxy, ; separated"))};
}

// Applies the given fields to `def`, validated as the dialog's OK does.
// Returns why they are not valid, empty when they are.
QString applyMiniAppFields(const CommandContext &ctx, MiniAppDefinition *def)
{
    const auto has = [&ctx](const char *name) { return ctx.has(QLatin1String(name)); };
    if (has("name"))
        def->name = ctx.str(QStringLiteral("name")).trimmed();
    if (has("url"))
        def->url = ctx.str(QStringLiteral("url")).trimmed();
    if (has("group"))
        def->group = ctx.str(QStringLiteral("group")).trimmed();
    if (has("command"))
        def->command = ctx.str(QStringLiteral("command")).trimmed();
    if (has("cwd"))
        def->cwd = ctx.str(QStringLiteral("cwd")).trimmed();
    if (has("env")) {
        const QJsonValue env = ctx.args().value(QStringLiteral("env"));
        if (env.isObject()) {
            QStringList lines;
            const QJsonObject o = env.toObject();
            for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
                if (!it.value().isString())
                    return QStringLiteral("env values must be strings (%1 is not)").arg(it.key());
                lines.append(it.key() + QLatin1Char('=') + it.value().toString());
            }
            def->env = lines.join(QLatin1Char('\n'));
        } else if (env.isString()) {
            def->env = env.toString();
        } else {
            return QStringLiteral("env must be KEY=VALUE lines or an object of strings");
        }
        // The dialog's env check: every non-comment line is KEY=VALUE.
        QStringList bad;
        const QStringList lines = def->env.split(QLatin1Char('\n'));
        for (qsizetype i = 0; i < lines.size(); ++i) {
            const QString line = lines.at(i).trimmed();
            if (!line.isEmpty() && !line.startsWith(QLatin1Char('#')) && line.indexOf(QLatin1Char('=')) <= 0)
                bad.append(QString::number(i + 1));
        }
        if (!bad.isEmpty())
            return QStringLiteral("env lines %1 are not KEY=VALUE").arg(bad.join(QStringLiteral(", ")));
    }
    if (has("healthCheckUrl")) {
        def->healthCheckUrl = ctx.str(QStringLiteral("healthCheckUrl")).trimmed();
        def->advancedEnabled = true;
    }
    if (has("healthTimeoutMs")) {
        const qint64 ms = ctx.integer(QStringLiteral("healthTimeoutMs"));
        if (ms < 5000 || ms > 300000)
            return QStringLiteral("healthTimeoutMs must be 5000-300000");
        def->healthTimeoutMs = int(ms);
        def->advancedEnabled = true;
    }
    if (has("advanced"))
        def->advancedEnabled = ctx.flag(QStringLiteral("advanced"));
    if (has("debugPort")) {
        const qint64 port = ctx.integer(QStringLiteral("debugPort"));
        if (port < 0 || port > 65535)
            return QStringLiteral("debugPort must be 0-65535");
        def->debugPort = int(port);
        def->debugEnabled = port > 0;
    }
    if (has("debug"))
        def->debugEnabled = ctx.flag(QStringLiteral("debug"));
    if (has("autoKillOnClose"))
        def->autoKillOnClose = ctx.flag(QStringLiteral("autoKillOnClose"));
    if (has("allowCrossOrigin"))
        def->allowCrossOrigin = ctx.flag(QStringLiteral("allowCrossOrigin"));
    if (has("proxy")) {
        def->proxyType = int(proxyTypes().indexOf(ctx.str(QStringLiteral("proxy"))));
        def->proxyEnabled = def->proxyType > 0;
    }
    if (has("proxyHost"))
        def->proxyHost = ctx.str(QStringLiteral("proxyHost")).trimmed();
    if (has("proxyPort")) {
        const qint64 port = ctx.integer(QStringLiteral("proxyPort"));
        if (port < 0 || port > 65535)
            return QStringLiteral("proxyPort must be 0-65535");
        def->proxyPort = int(port);
    }
    if (has("proxyBypass"))
        def->proxyBypassList = ctx.str(QStringLiteral("proxyBypass")).trimmed();

    if (def->name.isEmpty())
        return QStringLiteral("name is empty");
    const QUrl url(def->url);
    if (def->url.isEmpty() || !url.isValid()
        || (url.scheme() != QLatin1String("http") && url.scheme() != QLatin1String("https")))
        return QStringLiteral("url must start with http:// or https://");
    if (def->proxyEnabled && def->proxyHost.isEmpty())
        return QStringLiteral("a proxy needs proxyHost");
    return QString();
}

QJsonObject miniAppJson(const MiniAppDefinition &def, const QString &scope, const QString &workspace)
{
    // Never the env: it may hold secrets.
    QJsonObject o{{QStringLiteral("id"), def.id},
                  {QStringLiteral("name"), def.name},
                  {QStringLiteral("url"), def.url},
                  {QStringLiteral("scope"), scope},
                  {QStringLiteral("hasEnv"), !def.env.trimmed().isEmpty()},
                  {QStringLiteral("autoKillOnClose"), def.autoKillOnClose},
                  {QStringLiteral("allowCrossOrigin"), def.allowCrossOrigin}};
    if (!workspace.isEmpty())
        o.insert(QStringLiteral("workspace"), workspace);
    if (!def.group.isEmpty())
        o.insert(QStringLiteral("group"), def.group);
    if (!def.command.isEmpty())
        o.insert(QStringLiteral("command"), def.command);
    if (!def.cwd.isEmpty())
        o.insert(QStringLiteral("cwd"), def.cwd);
    o.insert(QStringLiteral("advanced"), def.advancedEnabled);
    if (!def.healthCheckUrl.isEmpty())
        o.insert(QStringLiteral("healthCheckUrl"), def.healthCheckUrl);
    o.insert(QStringLiteral("healthTimeoutMs"), def.healthTimeoutMs);
    o.insert(QStringLiteral("debug"), def.debugEnabled);
    if (def.debugPort > 0)
        o.insert(QStringLiteral("debugPort"), def.debugPort);
    const int proxy = std::clamp(def.proxyType, 0, int(proxyTypes().size()) - 1);
    o.insert(QStringLiteral("proxy"), def.proxyEnabled ? proxyTypes().at(proxy) : QStringLiteral("none"));
    if (!def.proxyHost.isEmpty()) {
        o.insert(QStringLiteral("proxyHost"), def.proxyHost);
        o.insert(QStringLiteral("proxyPort"), def.proxyPort);
    }
    if (!def.proxyBypassList.isEmpty())
        o.insert(QStringLiteral("proxyBypass"), def.proxyBypassList);
    return o;
}

// Where a mini app definition lives: the global list or one workspace's.
struct MiniAppSlot
{
    QString workspace; // empty: global
    QList<MiniAppDefinition> apps;
    qsizetype index = -1;
};

// Finds `id` in the global list, then in the workspace `workspaceHint`
// (default: the active one). False (and a 404 answered) when it is in neither.
bool findMiniApp(const CommandContext &ctx, MainWindow *w, MiniAppRegistry *registry, const QString &id,
                 MiniAppSlot *slot)
{
    QStringList scopes{QString()};
    const QString hint = ctx.has(QStringLiteral("workspace")) ? Registrar::pathArg(w, ctx.str(QStringLiteral("workspace")))
                                                              : Registrar::workspaceRoot(w);
    if (!hint.isEmpty())
        scopes.append(hint);
    for (const QString &scope : scopes) {
        const QList<MiniAppDefinition> apps = scope.isEmpty() ? registry->globalApps() : registry->workspaceApps(scope);
        for (qsizetype i = 0; i < apps.size(); ++i) {
            if (apps.at(i).id == id) {
                *slot = {scope, apps, i};
                return true;
            }
        }
    }
    ctx.fail(404, QStringLiteral("no mini app %1 (global or in %2); miniapps.list lists them")
                      .arg(id, hint.isEmpty() ? QStringLiteral("no workspace") : Registrar::displayPath(hint)));
    return false;
}

void storeMiniApps(MiniAppRegistry *registry, const QString &workspace, const QList<MiniAppDefinition> &apps)
{
    if (workspace.isEmpty())
        registry->setGlobalApps(apps);
    else
        registry->setWorkspaceApps(workspace, apps);
}

// The mini app or quick-browser tab `tab` names (answers the failure).
ads::CDockWidget *pinnableTabArg(const CommandContext &ctx, MainWindow *w, const MiniAppManager *manager)
{
    const int id = int(ctx.integer(QStringLiteral("tab")));
    ads::CDockWidget *dock = Registrar::tabById(w, id);
    if (!dock) {
        ctx.fail(404, QStringLiteral("no tab %1; tabs.list lists them").arg(id));
        return nullptr;
    }
    if (!manager->isPinnableTab(dock)) {
        ctx.fail(409, QStringLiteral("tab %1 is kind '%2', not a mini app or browser tab").arg(id).arg(Registrar::tabKind(dock)));
        return nullptr;
    }
    return dock;
}

WebViewWidget *webViewOf(ads::CDockWidget *dock)
{
    QWidget *content = dock ? dock->widget() : nullptr;
    if (!content)
        return nullptr;
    if (auto *web = qobject_cast<WebViewWidget *>(content))
        return web;
    return content->findChild<WebViewWidget *>();
}

// The browser tab `tab` names; else the one in front, else the newest.
WebViewWidget *browserArg(const CommandContext &ctx, MainWindow *w)
{
    const QList<ads::CDockWidget *> tabs = w->getDockedEditor()->allTabs();
    if (!ctx.has(QStringLiteral("tab"))) {
        WebViewWidget *newest = nullptr;
        for (ads::CDockWidget *dock : tabs) {
            if (WebViewWidget *web = webViewOf(dock)) {
                if (dock->isCurrentTab())
                    return web;
                newest = web;
            }
        }
        if (!newest)
            ctx.fail(404, QStringLiteral("no browser tab is open; open one with browser.open"));
        return newest;
    }
    const int id = int(ctx.integer(QStringLiteral("tab")));
    ads::CDockWidget *dock = Registrar::tabById(w, id);
    if (!dock) {
        ctx.fail(404, QStringLiteral("no tab %1; tabs.list lists them").arg(id));
        return nullptr;
    }
    WebViewWidget *web = webViewOf(dock);
    if (!web)
        ctx.fail(409, QStringLiteral("tab %1 is kind '%2', not a browser").arg(id).arg(Registrar::tabKind(dock)));
    return web;
}

QJsonObject browserJson(MainWindow *w, WebViewWidget *web)
{
    ads::CDockWidget *dock = Registrar::tabOf(web);
    QJsonObject o = dock ? Registrar::tabJson(w, dock) : QJsonObject();
    o.insert(QStringLiteral("url"), web->currentUrl());
    if (!web->cdpHttpUrl().isEmpty())
        o.insert(QStringLiteral("cdp"), web->cdpHttpUrl());
    return o;
}

QSet<ads::CDockWidget *> tabSet(MainWindow *w)
{
    const QList<ads::CDockWidget *> tabs = w->getDockedEditor()->allTabs();
    return {tabs.cbegin(), tabs.cend()};
}

ads::CDockWidget *newTab(MainWindow *w, const QSet<ads::CDockWidget *> &before)
{
    ads::CDockWidget *found = nullptr;
    const QList<ads::CDockWidget *> tabs = w->getDockedEditor()->allTabs();
    for (ads::CDockWidget *t : tabs) {
        if (!before.contains(t))
            found = t;
    }
    return found;
}

// Runs `js` in the page and answers with its JSON result.
void evaluate(const CommandContext &ctx, WebViewWidget *web, const QString &js, const QString &field)
{
    auto *wait = new QObject(web);
    QTimer::singleShot(30000, wait, [ctx, wait] {
        wait->deleteLater();
        ctx.fail(504, QStringLiteral("the page did not answer within 30 s"));
    });
    web->executeScript(js, [ctx, wait = QPointer<QObject>(wait), field](const QString &result) {
        if (wait)
            wait->deleteLater();
        if (result.isNull()) {
            ctx.fail(409, QStringLiteral("the page is not ready yet; try again in a moment"));
            return;
        }
        // WebView2 / WKWebView hand back the value as JSON text.
        QJsonParseError error{};
        const QJsonDocument doc = QJsonDocument::fromJson(QByteArray("[") + result.toUtf8() + ']', &error);
        const QJsonValue value = error.error == QJsonParseError::NoError ? doc.array().at(0) : QJsonValue(result);
        ctx.ok({{field, value}});
    });
}

} // namespace

void Registrar::preview()
{
    // --- preview.*: the live Markdown / HTML preview of an editor -------------
    add(QStringLiteral("preview.open"),
        QStringLiteral("Open (or bring forward) the live preview of a Markdown or HTML editor"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            PreviewTabManager *previews = w->app->getPreviewTabManager();
            if (!previews->canPreviewEditor(editor)) {
                ctx.fail(409, QStringLiteral("%1 has no live preview (Markdown and HTML do)").arg(editor->getName()));
                return;
            }
            const QSet<ads::CDockWidget *> before = tabSet(w);
            previews->openOrFocusPreview(editor);
            ads::CDockWidget *dock = newTab(w, before);
            if (!dock) {
                for (ads::CDockWidget *t : w->getDockedEditor()->allTabs()) {
                    if (t->isCurrentTab() && tabKind(t) != QLatin1String("editor"))
                        dock = t;
                }
            }
            if (dock)
                ctx.reveal(dock->widget());
            ctx.ok(dock ? tabJson(w, dock) : QJsonObject{{QStringLiteral("type"), previews->previewTypeName(editor)}});
        });

    add(QStringLiteral("preview.close"), QStringLiteral("Close the live preview of an editor"), true,
        {tabArg(QStringLiteral("editor tab id; default: the current editor"))},
        [w = w](const CommandContext &ctx) {
            ScintillaNext *editor = editorArg(ctx, w);
            if (!editor)
                return;
            w->app->getPreviewTabManager()->closePreview(editor);
            ctx.ok();
        });

    // --- browser.*: Quick Browser tabs and mini app pages ----------------------
    const Arg browserTab = tabArg(QStringLiteral("browser tab id; default: the one in front, else the newest"));

    add(QStringLiteral("browser.open"), QStringLiteral("Open a web page in a new browser tab"), true,
        {required(QStringLiteral("url"), ArgType::String,
                  QStringLiteral("address; a bare host gets https://, a local path opens as file://")),
         optional(QStringLiteral("cdp"), ArgType::Bool,
                  QStringLiteral("open a Chrome DevTools Protocol port for it (shown as `cdp`); default true")),
         optional(QStringLiteral("allowCrossOrigin"), ArgType::Bool,
                  QStringLiteral("let the page fetch other origins; default true"))},
        [w = w](const CommandContext &ctx) {
            const QUrl url(normalizeQuickBrowseInput(ctx.str(QStringLiteral("url")).trimmed()), QUrl::TolerantMode);
            if (!url.isValid() || url.isEmpty()) {
                ctx.fail(400, QStringLiteral("'%1' is not a valid address").arg(ctx.str(QStringLiteral("url"))));
                return;
            }
            const QSet<ads::CDockWidget *> before = tabSet(w);
            w->m_miniAppManager->launchQuickBrowser(url, ctx.flag(QStringLiteral("cdp"), true), 0, QString(), 0, QString(),
                                                    ctx.flag(QStringLiteral("allowCrossOrigin"), true));
            ads::CDockWidget *dock = newTab(w, before);
            WebViewWidget *web = webViewOf(dock);
            if (!web) {
                ctx.fail(422, QStringLiteral("no browser opened (this platform opens pages in the system browser)"));
                return;
            }
            ctx.reveal(dock->widget());
            QJsonObject o = browserJson(w, web);
            o.insert(QStringLiteral("url"), url.toString());
            ctx.ok(o);
        });

    add(QStringLiteral("browser.navigate"), QStringLiteral("Load an address in a browser tab"), true,
        {browserTab, required(QStringLiteral("url"), ArgType::String, QStringLiteral("address to load"))},
        [w = w](const CommandContext &ctx) {
            WebViewWidget *web = browserArg(ctx, w);
            if (!web)
                return;
            const QUrl url(normalizeQuickBrowseInput(ctx.str(QStringLiteral("url")).trimmed()), QUrl::TolerantMode);
            if (!url.isValid() || url.isEmpty()) {
                ctx.fail(400, QStringLiteral("'%1' is not a valid address").arg(ctx.str(QStringLiteral("url"))));
                return;
            }
            ctx.reveal(web);
            web->navigate(url);
            QJsonObject o = browserJson(w, web);
            o.insert(QStringLiteral("url"), url.toString());
            ctx.ok(o);
        });

    add(QStringLiteral("browser.go"), QStringLiteral("Go back, forward, reload or stop in a browser tab"), true,
        {browserTab,
         oneOf(QStringLiteral("action"),
               {QStringLiteral("back"), QStringLiteral("forward"), QStringLiteral("reload"), QStringLiteral("stop")},
               QStringLiteral("what to do"), true)},
        [w = w](const CommandContext &ctx) {
            WebViewWidget *web = browserArg(ctx, w);
            if (!web)
                return;
            const QString action = ctx.str(QStringLiteral("action"));
            ctx.reveal(web);
            if (action == QLatin1String("back"))
                web->goBack();
            else if (action == QLatin1String("forward"))
                web->goForward();
            else if (action == QLatin1String("reload"))
                web->reload();
            else
                web->stop();
            ctx.ok(browserJson(w, web));
        });

    add(QStringLiteral("browser.state"), QStringLiteral("Read a browser tab's address, title and DevTools port"), false,
        {browserTab}, [w = w](const CommandContext &ctx) {
            if (WebViewWidget *web = browserArg(ctx, w))
                ctx.ok(browserJson(w, web));
        });

    add(QStringLiteral("browser.read"), QStringLiteral("Read a browser tab's page as text, HTML or its title"), false,
        {browserTab,
         oneOf(QStringLiteral("as"), {QStringLiteral("text"), QStringLiteral("html"), QStringLiteral("title")},
               QStringLiteral("text (visible text, default), html (the DOM) or title")),
         optional(QStringLiteral("maxChars"), ArgType::Int, QStringLiteral("cut after this many characters; default 200000"))},
        [w = w](const CommandContext &ctx) {
            WebViewWidget *web = browserArg(ctx, w);
            if (!web)
                return;
            const QString as = ctx.str(QStringLiteral("as"), QStringLiteral("text"));
            const qint64 maxChars = std::clamp<qint64>(ctx.integer(QStringLiteral("maxChars"), 200000), 1, 10000000);
            const QString source = as == QLatin1String("html")    ? QStringLiteral("document.documentElement.outerHTML")
                                   : as == QLatin1String("title") ? QStringLiteral("document.title")
                                                                  : QStringLiteral("document.body ? document.body.innerText : ''");
            evaluate(ctx, web, QStringLiteral("(function(){var s=String(%1);return s.length>%2?s.slice(0,%2):s;})()")
                                   .arg(source).arg(maxChars),
                     as);
        });

    add(QStringLiteral("browser.eval"),
        QStringLiteral("Run JavaScript in a browser tab's page and return its (JSON) result"), true,
        {browserTab, required(QStringLiteral("script"), ArgType::String,
                              QStringLiteral("an expression or IIFE; its value must be JSON-serialisable"))},
        [w = w](const CommandContext &ctx) {
            WebViewWidget *web = browserArg(ctx, w);
            if (!web)
                return;
            ctx.reveal(web);
            evaluate(ctx, web, ctx.str(QStringLiteral("script")), QStringLiteral("result"));
        });

    add(QStringLiteral("browser.viewport"), QStringLiteral("Size a browser tab's page like a phone, tablet, desktop or the tab"),
        true,
        {browserTab, oneOf(QStringLiteral("mode"),
                           {QStringLiteral("fit"), QStringLiteral("mobile"), QStringLiteral("tablet"), QStringLiteral("pc")},
                           QStringLiteral("fit (the whole tab), mobile 390px, tablet 768px, pc 1280px"), true)},
        [w = w](const CommandContext &ctx) {
            WebViewWidget *web = browserArg(ctx, w);
            if (!web)
                return;
            const QString mode = ctx.str(QStringLiteral("mode"));
            web->setViewportMode(mode == QLatin1String("mobile")   ? WebViewWidget::ViewportMode::Mobile
                                 : mode == QLatin1String("tablet") ? WebViewWidget::ViewportMode::Tablet
                                 : mode == QLatin1String("pc")     ? WebViewWidget::ViewportMode::Pc
                                                                   : WebViewWidget::ViewportMode::Fit);
            ctx.reveal(web);
            ctx.ok(browserJson(w, web));
        });

    // --- miniapps.* ---------------------------------------------------------------
    add(QStringLiteral("miniapps.list"),
        QStringLiteral("List the configured mini apps: the global ones and a workspace's (never their env)"), false,
        {workspaceArgSpec()},
        [w = w](const CommandContext &ctx) {
            QJsonArray list;
            for (const MiniAppDefinition &def : w->m_miniAppRegistry->globalApps())
                list.append(miniAppJson(def, QStringLiteral("global"), QString()));
            const QString root = ctx.has(QStringLiteral("workspace")) ? pathArg(w, ctx.str(QStringLiteral("workspace")))
                                                                      : workspaceRoot(w);
            if (!root.isEmpty()) {
                for (const MiniAppDefinition &def : w->m_miniAppRegistry->workspaceApps(root))
                    list.append(miniAppJson(def, QStringLiteral("workspace"), displayPath(root)));
            }
            ctx.ok({{QStringLiteral("apps"), list}});
        });

    {
        QList<Arg> args{oneOf(QStringLiteral("scope"), {QStringLiteral("global"), QStringLiteral("workspace")},
                              QStringLiteral("global (every workspace) or workspace (only that workspace's menu); "
                                             "default global")),
                        workspaceArgSpec()};
        args.append(miniAppFieldArgs(true));
        add(QStringLiteral("miniapps.add"),
            QStringLiteral("Add a mini app (as Browser > Edit Mini Apps does); launch it with miniapps.launch"), true, args,
            [w = w](const CommandContext &ctx) {
                QString workspace;
                if (ctx.str(QStringLiteral("scope")) == QLatin1String("workspace")) {
                    workspace = ctx.has(QStringLiteral("workspace")) ? pathArg(w, ctx.str(QStringLiteral("workspace")))
                                                                     : workspaceRoot(w);
                    if (workspace.isEmpty()) {
                        ctx.fail(409, QStringLiteral("no workspace is open; pass `workspace` or use scope global"),
                                 {{QStringLiteral("needs"), QStringLiteral("workspace")}});
                        return;
                    }
                }
                MiniAppDefinition def;
                if (const QString error = applyMiniAppFields(ctx, &def); !error.isEmpty()) {
                    ctx.fail(400, error);
                    return;
                }
                def.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
                QList<MiniAppDefinition> apps = workspace.isEmpty() ? w->m_miniAppRegistry->globalApps()
                                                                    : w->m_miniAppRegistry->workspaceApps(workspace);
                apps.append(def);
                storeMiniApps(w->m_miniAppRegistry, workspace, apps);
                ctx.ok(miniAppJson(def, workspace.isEmpty() ? QStringLiteral("global") : QStringLiteral("workspace"),
                                   displayPath(workspace)));
            });
    }

    {
        QList<Arg> args{required(QStringLiteral("id"), ArgType::String, QStringLiteral("app id from miniapps.list")),
                        optional(QStringLiteral("workspace"), ArgType::String,
                                 QStringLiteral("the workspace whose mini app it is; default: the active workspace")),
                        oneOf(QStringLiteral("scope"), {QStringLiteral("global"), QStringLiteral("workspace")},
                              QStringLiteral("move it to the global list or to the workspace's"))};
        args.append(miniAppFieldArgs(false));
        add(QStringLiteral("miniapps.update"),
            QStringLiteral("Change a mini app's fields (only the ones given); a running one picks them up when relaunched"),
            true, args, [w = w](const CommandContext &ctx) {
                MiniAppSlot slot;
                if (!findMiniApp(ctx, w, w->m_miniAppRegistry, ctx.str(QStringLiteral("id")), &slot))
                    return;
                MiniAppDefinition def = slot.apps.at(slot.index);
                if (const QString error = applyMiniAppFields(ctx, &def); !error.isEmpty()) {
                    ctx.fail(400, error);
                    return;
                }
                QString target = slot.workspace;
                if (ctx.has(QStringLiteral("scope"))) {
                    target = ctx.str(QStringLiteral("scope")) == QLatin1String("global") ? QString()
                             : !slot.workspace.isEmpty() ? slot.workspace
                             : ctx.has(QStringLiteral("workspace")) ? pathArg(w, ctx.str(QStringLiteral("workspace")))
                                                                    : workspaceRoot(w);
                    if (ctx.str(QStringLiteral("scope")) == QLatin1String("workspace") && target.isEmpty()) {
                        ctx.fail(409, QStringLiteral("no workspace is open to move it to"),
                                 {{QStringLiteral("needs"), QStringLiteral("workspace")}});
                        return;
                    }
                }
                if (target == slot.workspace) {
                    slot.apps[slot.index] = def;
                    storeMiniApps(w->m_miniAppRegistry, slot.workspace, slot.apps);
                } else {
                    slot.apps.removeAt(slot.index);
                    storeMiniApps(w->m_miniAppRegistry, slot.workspace, slot.apps);
                    QList<MiniAppDefinition> moved = target.isEmpty() ? w->m_miniAppRegistry->globalApps()
                                                                      : w->m_miniAppRegistry->workspaceApps(target);
                    moved.append(def);
                    storeMiniApps(w->m_miniAppRegistry, target, moved);
                }
                ctx.ok(miniAppJson(def, target.isEmpty() ? QStringLiteral("global") : QStringLiteral("workspace"),
                                   displayPath(target)));
            });
    }

    add(QStringLiteral("miniapps.remove"), QStringLiteral("Remove a mini app (a running one keeps running until its tab closes)"),
        true,
        {required(QStringLiteral("id"), ArgType::String, QStringLiteral("app id from miniapps.list")),
         optional(QStringLiteral("workspace"), ArgType::String,
                  QStringLiteral("the workspace whose mini app it is; default: the active workspace"))},
        [w = w](const CommandContext &ctx) {
            MiniAppSlot slot;
            if (!findMiniApp(ctx, w, w->m_miniAppRegistry, ctx.str(QStringLiteral("id")), &slot))
                return;
            const QString name = slot.apps.at(slot.index).name;
            slot.apps.removeAt(slot.index);
            storeMiniApps(w->m_miniAppRegistry, slot.workspace, slot.apps);
            ctx.ok({{QStringLiteral("removed"), name}});
        });

    add(QStringLiteral("miniapps.move"), QStringLiteral("Move a mini app within its list (its place in the menu)"), true,
        {required(QStringLiteral("id"), ArgType::String, QStringLiteral("app id from miniapps.list")),
         required(QStringLiteral("index"), ArgType::Int, QStringLiteral("new 0-based position in its list (global or workspace)")),
         optional(QStringLiteral("workspace"), ArgType::String,
                  QStringLiteral("the workspace whose mini app it is; default: the active workspace"))},
        [w = w](const CommandContext &ctx) {
            MiniAppSlot slot;
            if (!findMiniApp(ctx, w, w->m_miniAppRegistry, ctx.str(QStringLiteral("id")), &slot))
                return;
            const qsizetype to = std::clamp<qsizetype>(ctx.integer(QStringLiteral("index")), 0, slot.apps.size() - 1);
            slot.apps.move(slot.index, to);
            storeMiniApps(w->m_miniAppRegistry, slot.workspace, slot.apps);
            ctx.ok({{QStringLiteral("index"), qint64(to)}});
        });

    add(QStringLiteral("miniapps.retry"),
        QStringLiteral("Restart a mini app tab (its command and page), as the tab menu's Retry does; for one that failed"),
        true, {required(QStringLiteral("tab"), ArgType::Int, QStringLiteral("the mini app's tab id (tabs.list)"))},
        [w = w](const CommandContext &ctx) {
            const int id = int(ctx.integer(QStringLiteral("tab")));
            ads::CDockWidget *dock = tabById(w, id);
            MiniAppInstance *instance = dock ? w->m_miniAppManager->instanceFor(dock) : nullptr;
            if (!instance) {
                ctx.fail(dock ? 409 : 404, dock ? QStringLiteral("tab %1 is not a mini app").arg(id)
                                                : QStringLiteral("no tab %1; tabs.list lists them").arg(id));
                return;
            }
            instance->retry();
            ctx.reveal(dock->widget());
            QJsonObject o = tabJson(w, dock);
            o.insert(QStringLiteral("state"),
                     QString::fromLatin1(QMetaEnum::fromType<MiniAppInstance::State>().valueToKey(instance->state())));
            ctx.ok(o);
        });

    add(QStringLiteral("miniapps.pin"),
        QStringLiteral("Pin or unpin a mini app or browser tab (a pinned tab has no close button and reopens with the session)"),
        true,
        {required(QStringLiteral("tab"), ArgType::Int, QStringLiteral("the mini app or browser tab id (tabs.list)")),
         required(QStringLiteral("pinned"), ArgType::Bool, QStringLiteral("true pins it, false unpins it"))},
        [w = w](const CommandContext &ctx) {
            ads::CDockWidget *dock = pinnableTabArg(ctx, w, w->m_miniAppManager);
            if (!dock)
                return;
            w->m_miniAppManager->setTabPinned(dock, ctx.flag(QStringLiteral("pinned")));
            ctx.reveal(dock->widget());
            QJsonObject o = tabJson(w, dock);
            o.insert(QStringLiteral("pinned"), MiniAppManager::isTabPinned(dock));
            ctx.ok(o);
        });

    add(QStringLiteral("miniapps.launch"),
        QStringLiteral("Launch a mini app (start its command, then open its page once it answers)"), true,
        {required(QStringLiteral("id"), ArgType::String, QStringLiteral("app id from miniapps.list"))},
        [w = w](const CommandContext &ctx) {
            const QString id = ctx.str(QStringLiteral("id"));
            const QList<MiniAppDefinition> apps = w->m_miniAppRegistry->mergedApps(w->currentWorkspaceRoot());
            const auto it = std::find_if(apps.cbegin(), apps.cend(), [&](const MiniAppDefinition &d) { return d.id == id; });
            if (it == apps.cend()) {
                ctx.fail(404, QStringLiteral("no mini app %1; miniapps.list lists them").arg(id));
                return;
            }
            const QSet<ads::CDockWidget *> before = tabSet(w);
            w->m_miniAppManager->launchApp(*it);
            ads::CDockWidget *dock = newTab(w, before);
            if (dock)
                ctx.reveal(dock->widget());
            QJsonObject o = dock ? tabJson(w, dock) : QJsonObject();
            o.insert(QStringLiteral("app"), it->name);
            if (!dock)
                o.insert(QStringLiteral("note"),
                         QStringLiteral("already running (its tab is in front), or its page opens once the command answers; "
                                        "tabs.list shows it"));
            ctx.ok(o);
        });
}

} // namespace control::commands
