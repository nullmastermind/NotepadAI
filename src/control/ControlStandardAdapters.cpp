/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// Adapters for Qt's own composite widgets: item views (rows) and tab bars
// (tabs). Custom NotepadAI widgets register theirs elsewhere.

#include "ControlAdapter.h"
#include "ControlIds.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractProxyModel>
#include <QDir>
#include <QDockWidget>
#include <QFileSystemModel>
#include <QHeaderView>
#include <QJsonArray>
#include <QListView>
#include <QTabBar>
#include <QTableView>
#include <QTreeView>

namespace control {

namespace {

QString escapePart(const QString &s)
{
    QString out = s;
    out.replace(QLatin1Char('%'), QLatin1String("%25"));
    out.replace(QLatin1Char('/'), QLatin1String("%2F"));
    return out;
}

QString unescapePart(const QString &s)
{
    QString out = s;
    out.replace(QLatin1String("%2F"), QLatin1String("/"), Qt::CaseInsensitive);
    out.replace(QLatin1String("%25"), QLatin1String("%"));
    return out;
}

bool isFileSystemModel(const QAbstractItemModel *model)
{
    while (model) {
        if (qobject_cast<const QFileSystemModel *>(model))
            return true;
        const auto *proxy = qobject_cast<const QAbstractProxyModel *>(model);
        model = proxy ? proxy->sourceModel() : nullptr;
    }
    return false;
}

QString cleanPath(const QString &p)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(p));
}

class ItemViewAdapter : public ControlAdapter
{
public:
    QString name() const override { return QStringLiteral("itemView"); }

    bool matches(const QWidget *w) const override
    {
        return qobject_cast<const QAbstractItemView *>(w) && !qobject_cast<const QHeaderView *>(w);
    }

    QString role(const QWidget *w) const override
    {
        if (qobject_cast<const QTreeView *>(w))
            return QStringLiteral("tree");
        if (qobject_cast<const QTableView *>(w))
            return QStringLiteral("table");
        return QStringLiteral("list");
    }

    QJsonObject summary(QWidget *w) const override
    {
        auto *view = static_cast<QAbstractItemView *>(w);
        QJsonObject o;
        if (!view->model())
            return o;
        o.insert(QStringLiteral("rows"), view->model()->rowCount(view->rootIndex()));
        const QModelIndex current = view->currentIndex();
        if (current.isValid())
            o.insert(QStringLiteral("current"), keyOf(view, current));
        if (view->selectionModel())
            o.insert(QStringLiteral("selected"), int(view->selectionModel()->selectedRows().size()));
        return o;
    }

    QList<Part> parts(QWidget *w, int limit) const override
    {
        auto *view = static_cast<QAbstractItemView *>(w);
        QList<Part> out;
        visibleRows(view, 0, limit, [&](const QModelIndex &index, int depth) {
            out.append(partFor(view, index, depth));
        }, false);
        return out;
    }

    bool read(QWidget *w, const QString &part, const ReadOptions &opts, QJsonObject &out) const override
    {
        auto *view = static_cast<QAbstractItemView *>(w);
        if (!view->model())
            return false;
        if (!part.isEmpty()) {
            const QModelIndex index = find(view, part);
            if (!index.isValid())
                return false;
            out = rowJson(view, index, 0);
            return true;
        }
        QJsonArray rows;
        const int count = opts.count < 0 ? 200 : int(qMin<qint64>(opts.count, 5000));
        const int total = visibleRows(view, int(opts.from), count, [&](const QModelIndex &index, int depth) {
            rows.append(rowJson(view, index, depth));
        });
        out.insert(QStringLiteral("rows"), rows);
        out.insert(QStringLiteral("from"), opts.from);
        out.insert(QStringLiteral("visibleRows"), total);
        if (auto *table = qobject_cast<QTableView *>(view)) {
            QJsonArray headers;
            for (int c = 0; c < view->model()->columnCount(view->rootIndex()); ++c) {
                if (!table->isColumnHidden(c))
                    headers.append(view->model()->headerData(c, Qt::Horizontal).toString());
            }
            out.insert(QStringLiteral("columns"), headers);
        }
        return true;
    }

    void reveal(QWidget *w, const QString &part) const override
    {
        auto *view = static_cast<QAbstractItemView *>(w);
        const QModelIndex index = find(view, part);
        if (index.isValid())
            view->scrollTo(index);
    }

    QRect partRect(QWidget *w, const QString &part) const override
    {
        auto *view = static_cast<QAbstractItemView *>(w);
        const QModelIndex index = find(view, part);
        return index.isValid() ? view->visualRect(index) & view->viewport()->rect() : QRect();
    }

    QWidget *eventTarget(QWidget *w) const override { return static_cast<QAbstractItemView *>(w)->viewport(); }

    std::optional<QString> perform(QWidget *w, const QString &part, const QString &op,
                                   const QJsonObject &) const override
    {
        auto *tree = qobject_cast<QTreeView *>(w);
        if (!tree || (op != QLatin1String("expand") && op != QLatin1String("collapse")))
            return std::nullopt;
        const QModelIndex index = find(tree, part);
        if (!index.isValid())
            return QStringLiteral("no row '%1'").arg(part);
        if (op == QLatin1String("expand"))
            tree->expand(index);
        else
            tree->collapse(index);
        return QString();
    }

private:
    // Rows in the order the user sees them (expanded tree rows included).
    // Calls fn for rows [from, from+count); returns how many rows were seen.
    template <typename Fn>
    int visibleRows(QAbstractItemView *view, int from, int count, Fn fn, bool needTotal = true) const
    {
        QAbstractItemModel *model = view->model();
        if (!model)
            return 0;
        auto *tree = qobject_cast<QTreeView *>(view);
        int seen = 0;
        constexpr int kScanCap = 100000; // never walk a giant model to the end
        std::function<void(const QModelIndex &, int)> walk = [&](const QModelIndex &parent, int depth) {
            const int rows = model->rowCount(parent);
            for (int r = 0; r < rows && seen < kScanCap; ++r) {
                if (!needTotal && seen >= from + count)
                    return;
                if (tree && tree->isRowHidden(r, parent))
                    continue;
                if (auto *list = qobject_cast<QListView *>(view); list && list->isRowHidden(r))
                    continue;
                if (auto *table = qobject_cast<QTableView *>(view); table && table->isRowHidden(r))
                    continue;
                const QModelIndex index = model->index(r, 0, parent);
                if (seen >= from && seen < from + count)
                    fn(index, depth);
                ++seen;
                if (tree && tree->isExpanded(index))
                    walk(index, depth + 1);
            }
        };
        walk(view->rootIndex(), 0);
        return seen;
    }

    QString keyOf(const QAbstractItemView *view, const QModelIndex &index) const
    {
        if (isFileSystemModel(view->model())) {
            const QString path = index.data(QFileSystemModel::FilePathRole).toString();
            if (!path.isEmpty())
                return QStringLiteral("path:") + cleanPath(path);
        }
        QStringList segments;
        for (QModelIndex i = index; i.isValid() && i != view->rootIndex(); i = i.parent())
            segments.prepend(escapePart(i.siblingAtColumn(0).data(Qt::DisplayRole).toString()));
        return QStringLiteral("row:") + segments.join(QLatin1Char('/'));
    }

    QModelIndex find(const QAbstractItemView *view, const QString &key) const
    {
        QAbstractItemModel *model = view->model();
        if (!model)
            return {};
        if (key.startsWith(QLatin1String("path:"))) {
            const QString target = cleanPath(key.mid(5));
            const QModelIndexList hits = model->match(model->index(0, 0, view->rootIndex()),
                                                      QFileSystemModel::FilePathRole, target, 1,
                                                      Qt::MatchFixedString | Qt::MatchRecursive);
            if (!hits.isEmpty())
                return hits.first();
            // The root row itself (a workspace's top node) is not under rootIndex's first child.
            for (int r = 0; r < model->rowCount(view->rootIndex()); ++r) {
                const QModelIndex i = model->index(r, 0, view->rootIndex());
                if (cleanPath(i.data(QFileSystemModel::FilePathRole).toString()).compare(target, Qt::CaseInsensitive) == 0)
                    return i;
            }
            return {};
        }
        if (key.startsWith(QLatin1String("index:"))) {
            QModelIndex parent = view->rootIndex();
            QModelIndex index;
            for (const QString &s : key.mid(6).split(QLatin1Char('/'))) {
                bool ok = false;
                const int row = s.toInt(&ok);
                if (!ok || row < 0 || row >= model->rowCount(parent))
                    return {};
                index = model->index(row, 0, parent);
                parent = index;
            }
            return index;
        }
        if (key.startsWith(QLatin1String("row:"))) {
            QModelIndex parent = view->rootIndex();
            QModelIndex index;
            for (const QString &s : key.mid(4).split(QLatin1Char('/'))) {
                const QString text = unescapePart(s);
                index = QModelIndex();
                for (int r = 0; r < model->rowCount(parent); ++r) {
                    const QModelIndex candidate = model->index(r, 0, parent);
                    if (candidate.data(Qt::DisplayRole).toString() == text) {
                        index = candidate;
                        break;
                    }
                }
                if (!index.isValid())
                    return {};
                parent = index;
            }
            return index;
        }
        return {};
    }

    Part partFor(const QAbstractItemView *view, const QModelIndex &index, int depth) const
    {
        Part p;
        p.key = keyOf(view, index);
        p.role = QStringLiteral("row");
        p.label = index.data(Qt::DisplayRole).toString();
        p.enabled = index.flags().testFlag(Qt::ItemIsEnabled);
        p.selected = view->selectionModel() && view->selectionModel()->isSelected(index);
        p.current = view->currentIndex() == index;
        if (depth > 0)
            p.extra.insert(QStringLiteral("depth"), depth);
        if (const auto *tree = qobject_cast<const QTreeView *>(view); tree && view->model()->hasChildren(index))
            p.extra.insert(QStringLiteral("expanded"), tree->isExpanded(index));
        return p;
    }

    QJsonObject rowJson(const QAbstractItemView *view, const QModelIndex &index, int depth) const
    {
        const Part p = partFor(view, index, depth);
        QJsonObject o = p.extra;
        o.insert(QStringLiteral("key"), p.key);
        o.insert(QStringLiteral("text"), p.label);
        if (p.selected)
            o.insert(QStringLiteral("selected"), true);
        if (p.current)
            o.insert(QStringLiteral("current"), true);
        if (!p.enabled)
            o.insert(QStringLiteral("enabled"), false);
        const int columns = view->model()->columnCount(index.parent());
        if (columns > 1) {
            QJsonArray cells;
            for (int c = 0; c < columns; ++c)
                cells.append(index.siblingAtColumn(c).data(Qt::DisplayRole).toString());
            o.insert(QStringLiteral("cells"), cells);
        }
        const Qt::CheckState check = Qt::CheckState(index.data(Qt::CheckStateRole).toInt());
        if (index.data(Qt::CheckStateRole).isValid())
            o.insert(QStringLiteral("checked"), check == Qt::Checked);
        return o;
    }
};

class TabBarAdapter : public ControlAdapter
{
public:
    QString name() const override { return QStringLiteral("tabBar"); }
    bool matches(const QWidget *w) const override { return qobject_cast<const QTabBar *>(w) != nullptr; }
    QString role(const QWidget *) const override { return QStringLiteral("tabs"); }

    QJsonObject summary(QWidget *w) const override
    {
        auto *bar = static_cast<QTabBar *>(w);
        return {{QStringLiteral("current"), bar->currentIndex() >= 0 ? keyOf(bar, bar->currentIndex()) : QString()},
                {QStringLiteral("count"), bar->count()}};
    }

    QList<Part> parts(QWidget *w, int limit) const override
    {
        auto *bar = static_cast<QTabBar *>(w);
        QList<Part> out;
        for (int i = 0; i < bar->count() && i < limit; ++i) {
            if (!bar->isTabVisible(i))
                continue;
            Part p;
            p.key = keyOf(bar, i);
            p.role = QStringLiteral("tab");
            p.label = bar->tabText(i).remove(QLatin1Char('&'));
            if (p.label.isEmpty())
                p.label = bar->tabToolTip(i);
            p.enabled = bar->isTabEnabled(i);
            p.current = bar->currentIndex() == i;
            out.append(p);
        }
        return out;
    }

    QRect partRect(QWidget *w, const QString &part) const override
    {
        auto *bar = static_cast<QTabBar *>(w);
        const int i = indexOf(bar, part);
        return i < 0 ? QRect() : bar->tabRect(i);
    }

    std::optional<QString> perform(QWidget *w, const QString &part, const QString &op,
                                   const QJsonObject &) const override
    {
        if (op != QLatin1String("close"))
            return std::nullopt;
        auto *bar = static_cast<QTabBar *>(w);
        const int i = indexOf(bar, part);
        if (i < 0)
            return QStringLiteral("no tab '%1'").arg(part);
        // The tab's close button, as the user would click it.
        for (auto side : {QTabBar::RightSide, QTabBar::LeftSide}) {
            if (auto *button = qobject_cast<QAbstractButton *>(bar->tabButton(i, side))) {
                button->click();
                return QString();
            }
        }
        if (!bar->tabsClosable())
            return QStringLiteral("this tab cannot be closed");
        emit bar->tabCloseRequested(i);
        return QString();
    }

private:
    static QString keyOf(const QTabBar *bar, int i)
    {
        const QVariant data = bar->tabData(i);
        if (data.canConvert<QString>() && !data.toString().isEmpty())
            return QStringLiteral("tab:") + data.toString();
        return QStringLiteral("tab:") + QString::number(i);
    }

    static int indexOf(const QTabBar *bar, const QString &key)
    {
        for (int i = 0; i < bar->count(); ++i) {
            if (keyOf(bar, i) == key)
                return i;
        }
        return -1;
    }
};

// A QDockWidget (the workspace panels). Its title bar's Close and Float are
// Qt's own buttons, which have no ids, so the panel takes their actions.
class PanelAdapter : public ControlAdapter
{
public:
    QString name() const override { return QStringLiteral("panel"); }
    bool matches(const QWidget *w) const override { return qobject_cast<const QDockWidget *>(w) != nullptr; }
    QString role(const QWidget *) const override { return QStringLiteral("dock"); }
    bool walksChildren(const QWidget *) const override { return true; }

    bool reachableWhileHidden(const QWidget *w, const QString &op) const override
    {
        // A panel tabbed behind another is out of view, yet its tab can be
        // clicked and its group's edge dragged.
        return isOpen(static_cast<const QDockWidget *>(w))
               && (op == QLatin1String("select") || op == QLatin1String("show") || op == QLatin1String("close")
                   || op == QLatin1String("float") || op == QLatin1String("resize"));
    }

    QJsonObject summary(QWidget *w) const override
    {
        auto *dock = static_cast<QDockWidget *>(w);
        QJsonObject o{{QStringLiteral("title"), dock->windowTitle()},
                      {QStringLiteral("kind"), QStringLiteral("panel")},
                      {QStringLiteral("open"), isOpen(dock)}};
        if (isOpen(dock))
            o.insert(QStringLiteral("current"), dock->isVisible() && !ids::isBehindTab(dock));
        if (dock->isFloating())
            o.insert(QStringLiteral("floating"), true);
        return o;
    }

    std::optional<QString> perform(QWidget *w, const QString &part, const QString &op,
                                   const QJsonObject &) const override
    {
        if (!part.isEmpty())
            return std::nullopt;
        auto *dock = static_cast<QDockWidget *>(w);
        if (op == QLatin1String("select") || op == QLatin1String("show")) {
            // A click on its tab (raise() makes a tabbed panel current), or
            // the View menu entry for a closed one.
            if (!isOpen(dock))
                dock->toggleViewAction()->trigger();
            dock->raise();
            if (QWidget *content = dock->widget(); content && content->focusPolicy() != Qt::NoFocus)
                content->setFocus(Qt::MouseFocusReason);
            return QString();
        }
        if (op == QLatin1String("close")) {
            if (!isOpen(dock))
                return QStringLiteral("it is already closed");
            if (!dock->features().testFlag(QDockWidget::DockWidgetClosable))
                return QStringLiteral("this panel cannot be closed");
            dock->close(); // its title bar's close button
            return QString();
        }
        if (op == QLatin1String("float")) {
            if (!dock->features().testFlag(QDockWidget::DockWidgetFloatable))
                return QStringLiteral("this panel cannot float");
            dock->setFloating(!dock->isFloating()); // its title bar's float button
            return QString();
        }
        return std::nullopt;
    }

private:
    // Closed panels have their View menu entry unchecked; a panel tabbed
    // behind another is out of view but still checked.
    static bool isOpen(const QDockWidget *dock) { return dock->toggleViewAction()->isChecked(); }
};

} // namespace

ControlAdapters &ControlAdapters::instance()
{
    static ControlAdapters adapters;
    return adapters;
}

ControlAdapters::ControlAdapters()
{
    m_adapters.push_back(std::make_unique<ItemViewAdapter>());
    m_adapters.push_back(std::make_unique<TabBarAdapter>());
    m_adapters.push_back(std::make_unique<PanelAdapter>());
}

void ControlAdapters::add(std::unique_ptr<ControlAdapter> adapter)
{
    m_adapters.insert(m_adapters.begin(), std::move(adapter));
}

const ControlAdapter *ControlAdapters::find(const QWidget *widget) const
{
    for (const auto &a : m_adapters) {
        if (a->matches(widget))
            return a.get();
    }
    return nullptr;
}

} // namespace control
