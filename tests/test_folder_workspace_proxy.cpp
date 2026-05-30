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

#include <QtTest>
#include <QAbstractItemModelTester>
#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QTextStream>

#include "docks/FolderAsWorkspaceFsModel.h"
#include "docks/FolderAsWorkspaceProxyModel.h"

// Pure proxy unit test, no live git. Builds a known QTemporaryDir tree, wires a
// FolderAsWorkspaceFsModel source under a FolderAsWorkspaceProxyModel, and
// exercises the synthetic-root mapping, structure, signal forwarding, and
// re-rooting in isolation.
class TestFolderWorkspaceProxy : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase();

    void topNode_singleRowMapsToRoot();
    void parents_rootAndChild();
    void roundTrip_allThreeRegions();
    void modelTester_conformance();
    void forwarding_insertAndRemove();
    void selectionSurvivesLayoutChange();
    void reRoot_swapsTopNode();
    void emptyRoot_zeroRows();

private:
    // Build root/, root/a.txt, root/sub/, root/sub/b.txt, root/sub/deep/,
    // root/sub/deep/c.txt under `base`.
    static void buildTree(const QString &base);
    static void writeFile(const QString &path, const QString &content);

    // Wait until `model` has loaded `dir`'s children (directoryLoaded fired and
    // rowCount > 0). Returns the proxy index of `dir` for convenience.
    static bool waitForLoaded(FolderAsWorkspaceFsModel *model, const QString &dir);
};

void TestFolderWorkspaceProxy::writeFile(const QString &path, const QString &content)
{
    QFile f(path);
    QVERIFY2(f.open(QIODevice::WriteOnly | QIODevice::Text),
             qPrintable(QStringLiteral("cannot create %1").arg(path)));
    QTextStream(&f) << content;
    f.close();
}

void TestFolderWorkspaceProxy::buildTree(const QString &base)
{
    QDir d(base);
    QVERIFY(d.mkpath(QStringLiteral("sub/deep")));
    writeFile(base + QStringLiteral("/a.txt"), QStringLiteral("a"));
    writeFile(base + QStringLiteral("/sub/b.txt"), QStringLiteral("b"));
    writeFile(base + QStringLiteral("/sub/deep/c.txt"), QStringLiteral("c"));
}

bool TestFolderWorkspaceProxy::waitForLoaded(FolderAsWorkspaceFsModel *model, const QString &dir)
{
    const QModelIndex srcIdx = model->index(dir);
    if (!srcIdx.isValid())
        return false;
    // QFileSystemModel populates on a gatherer thread; spin the event loop until
    // the directory's children materialize.
    bool ok = QTest::qWaitFor([&]() {
        return model->rowCount(model->index(dir)) > 0;
    }, 5000);
    return ok;
}

void TestFolderWorkspaceProxy::initTestCase()
{
    // QFileSystemModel needs a running event loop / gatherer thread; QTEST_MAIN
    // provides a QCoreApplication, which is sufficient for the model.
}

void TestFolderWorkspaceProxy::topNode_singleRowMapsToRoot()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString base = QDir::cleanPath(tmp.path());
    buildTree(base);

    FolderAsWorkspaceFsModel model;
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(base);
    proxy.setRootSourcePath(base);

    QVERIFY(waitForLoaded(&model, base));

    // Exactly one top-level row, and it maps to the workspace root path.
    QCOMPARE(proxy.rowCount(QModelIndex()), 1);
    QCOMPARE(proxy.columnCount(QModelIndex()), 1);

    const QModelIndex top = proxy.index(0, 0, QModelIndex());
    QVERIFY(top.isValid());
    QCOMPARE(QDir::cleanPath(proxy.filePath(top)), base);
    QVERIFY(proxy.isDir(top));
    QVERIFY(proxy.hasChildren(top));
}

void TestFolderWorkspaceProxy::parents_rootAndChild()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString base = QDir::cleanPath(tmp.path());
    buildTree(base);

    FolderAsWorkspaceFsModel model;
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(base);
    proxy.setRootSourcePath(base);
    QVERIFY(waitForLoaded(&model, base));

    const QModelIndex pr = proxy.index(0, 0, QModelIndex());
    QVERIFY(pr.isValid());
    // The synthetic root closes the tree at the top — no phantom parent.
    QCOMPARE(proxy.parent(pr), QModelIndex());

    // A child of the root has the root as its parent.
    QVERIFY(proxy.rowCount(pr) >= 1);
    const QModelIndex c0 = proxy.index(0, 0, pr);
    QVERIFY(c0.isValid());
    QCOMPARE(proxy.parent(c0), pr);
}

void TestFolderWorkspaceProxy::roundTrip_allThreeRegions()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString base = QDir::cleanPath(tmp.path());
    buildTree(base);

    FolderAsWorkspaceFsModel model;
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(base);
    proxy.setRootSourcePath(base);
    QVERIFY(waitForLoaded(&model, base));

    // Region 1: synthetic root.
    const QModelIndex rootSrc = model.index(base);
    QVERIFY(rootSrc.isValid());
    const QModelIndex pr = proxy.mapFromSource(rootSrc);
    QVERIFY(pr.isValid());
    QCOMPARE(proxy.mapToSource(pr), rootSrc);
    QCOMPARE(proxy.mapFromSource(proxy.mapToSource(pr)), pr);

    // Region 2: a direct child of the root (sub/).
    const QString subPath = base + QStringLiteral("/sub");
    QVERIFY(waitForLoaded(&model, subPath));
    const QModelIndex subSrc = model.index(subPath);
    QVERIFY(subSrc.isValid());
    const QModelIndex subProxy = proxy.mapFromSource(subSrc);
    QVERIFY(subProxy.isValid());
    QCOMPARE(proxy.mapToSource(subProxy), subSrc);
    QCOMPARE(proxy.mapFromSource(proxy.mapToSource(subProxy)), subProxy);
    QCOMPARE(proxy.parent(subProxy), pr);

    // Region 3: a deep node (sub/deep/c.txt) — passthrough far below the root.
    const QString deepPath = base + QStringLiteral("/sub/deep");
    QVERIFY(waitForLoaded(&model, deepPath));
    const QString leafPath = deepPath + QStringLiteral("/c.txt");
    const QModelIndex leafSrc = model.index(leafPath);
    QVERIFY(leafSrc.isValid());
    const QModelIndex leafProxy = proxy.mapFromSource(leafSrc);
    QVERIFY(leafProxy.isValid());
    QCOMPARE(proxy.mapToSource(leafProxy), leafSrc);
    QCOMPARE(proxy.mapFromSource(proxy.mapToSource(leafProxy)), leafProxy);
    QCOMPARE(QDir::cleanPath(proxy.filePath(leafProxy)), QDir::cleanPath(leafPath));
}

void TestFolderWorkspaceProxy::modelTester_conformance()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString base = QDir::cleanPath(tmp.path());
    buildTree(base);

    FolderAsWorkspaceFsModel model;
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(base);
    proxy.setRootSourcePath(base);
    QVERIFY(waitForLoaded(&model, base));

    // Pre-load every directory we touch BEFORE constructing the tester so it
    // observes a fully settled model. QFileSystemModel populates on a gatherer
    // thread and emits layoutChanged/dataChanged during async load that can trip
    // QAbstractItemModelTester's conformance checks for reasons unrelated to this
    // proxy. Settling first lets us use the strict (default QtTest) reporting
    // mode — which actually FAILS the test on a violation — instead of the
    // permissive Warning mode that only logs.
    const QString subPath = base + QStringLiteral("/sub");
    QVERIFY(waitForLoaded(&model, subPath));
    const QString deepPath = base + QStringLiteral("/sub/deep");
    QVERIFY(waitForLoaded(&model, deepPath));

    // Construct the tester AFTER the model has settled; it validates begin/end
    // pairing and index consistency continuously. Default reporting mode (QtTest)
    // turns any conformance violation into a test failure.
    QAbstractItemModelTester tester(&proxy);

    // Touch the proxy structure (already-loaded nodes only) to ensure consistency
    // under the tester without kicking off a fresh async fetch.
    const QModelIndex pr = proxy.index(0, 0, QModelIndex());
    QVERIFY(pr.isValid());
    QVERIFY(proxy.rowCount(pr) >= 1);
    const QModelIndex subProxy = proxy.mapFromSource(model.index(subPath));
    QVERIFY(subProxy.isValid());
    QVERIFY(proxy.rowCount(subProxy) >= 1);
}

void TestFolderWorkspaceProxy::forwarding_insertAndRemove()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString base = QDir::cleanPath(tmp.path());
    buildTree(base);

    FolderAsWorkspaceFsModel model;
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(base);
    proxy.setRootSourcePath(base);
    QVERIFY(waitForLoaded(&model, base));

    // Make sure sub/ is loaded so its row insert/remove is watched.
    const QString subPath = base + QStringLiteral("/sub");
    QVERIFY(waitForLoaded(&model, subPath));
    const QModelIndex subProxy = proxy.mapFromSource(model.index(subPath));
    QVERIFY(subProxy.isValid());
    const int before = proxy.rowCount(subProxy);

    QSignalSpy insertSpy(&proxy, &QAbstractItemModel::rowsInserted);
    QVERIFY(insertSpy.isValid());

    // Create a new file under the loaded sub/ directory.
    const QString newFile = subPath + QStringLiteral("/created.txt");
    writeFile(newFile, QStringLiteral("new"));

    QVERIFY(QTest::qWaitFor([&]() {
        return proxy.rowCount(proxy.mapFromSource(model.index(subPath))) == before + 1;
    }, 5000));
    QVERIFY(insertSpy.count() >= 1);

    // The new row is reachable through the proxy.
    const QModelIndex newProxy = proxy.mapFromSource(model.index(newFile));
    QVERIFY(newProxy.isValid());
    QCOMPARE(QDir::cleanPath(proxy.filePath(newProxy)), QDir::cleanPath(newFile));

    QSignalSpy removeSpy(&proxy, &QAbstractItemModel::rowsRemoved);
    QVERIFY(removeSpy.isValid());

    QVERIFY(QFile::remove(newFile));
    QVERIFY(QTest::qWaitFor([&]() {
        return proxy.rowCount(proxy.mapFromSource(model.index(subPath))) == before;
    }, 5000));
    QVERIFY(removeSpy.count() >= 1);
}

void TestFolderWorkspaceProxy::selectionSurvivesLayoutChange()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString base = QDir::cleanPath(tmp.path());
    buildTree(base);
    // Add extra root-level files so a name sort genuinely REORDERS rows: with
    // these, "a.txt" sits at row 0 ascending and moves toward the end descending.
    // A reorder is precisely what surfaces the stale-source-index bug (W1).
    writeFile(base + QStringLiteral("/mmm.txt"), QStringLiteral("m"));
    writeFile(base + QStringLiteral("/zzz.txt"), QStringLiteral("z"));

    FolderAsWorkspaceFsModel model;
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(base);
    proxy.setRootSourcePath(base);
    QVERIFY(waitForLoaded(&model, base));

    // Wait until all four root entries (a.txt, sub/, mmm.txt, zzz.txt) materialize.
    const QModelIndex pr = proxy.index(0, 0, QModelIndex());
    QVERIFY(pr.isValid());
    QVERIFY(QTest::qWaitFor([&]() { return proxy.rowCount(pr) >= 4; }, 5000));

    // Hold a QPersistentModelIndex on a known PROXY index (a.txt under the root),
    // mimicking how a view's selection survives a layout change.
    const QString filePath = base + QStringLiteral("/a.txt");
    const QModelIndex fileSrc = model.index(filePath);
    QVERIFY(fileSrc.isValid());
    const QModelIndex fileProxy = proxy.mapFromSource(fileSrc);
    QVERIFY(fileProxy.isValid());
    const QPersistentModelIndex heldProxy(fileProxy);
    QVERIFY(heldProxy.isValid());
    const QString originalPath = QDir::cleanPath(proxy.filePath(heldProxy));
    QCOMPARE(originalPath, QDir::cleanPath(filePath));

    // Establish a known ASCENDING baseline (QFileSystemModel::sort is synchronous)
    // and record a.txt's CURRENT proxy row straight from the source. This is ground
    // truth — computed independently of the persistent-index remap under test.
    model.sort(0, Qt::AscendingOrder);
    const int aRowAscending = proxy.mapFromSource(model.index(filePath)).row();

    QSignalSpy layoutSpy(&proxy, &QAbstractItemModel::layoutChanged);
    QVERIFY(layoutSpy.isValid());

    // Flip to DESCENDING: a genuine row reorder on the SOURCE model.
    // QFileSystemModel::sort emits layoutAboutToBeChanged/layoutChanged, which the
    // proxy forwards while remapping its persistent indices via
    // changePersistentIndexList (the W1 path).
    model.sort(0, Qt::DescendingOrder);
    QVERIFY(QTest::qWaitFor([&]() { return layoutSpy.count() >= 1; }, 5000));

    // The proxy must have re-emitted layoutChanged (the remap path was exercised).
    QVERIFY(layoutSpy.count() >= 1);

    // Freshly map a.txt to its proxy index AFTER the descending sort: again ground
    // truth, carrying a.txt's CURRENT row, independent of any held index.
    const QModelIndex fresh = proxy.mapFromSource(model.index(filePath));
    QVERIFY(fresh.isValid());

    // Prove a real reorder happened — a.txt's row genuinely changed between the two
    // orders. Without this the row-sensitive QCOMPARE below could pass trivially: a
    // held index that never needed remapping would match a file that never moved.
    QVERIFY2(fresh.row() != aRowAscending,
             "expected a.txt to occupy a different row after the descending sort");

    // ROW-SENSITIVE GUARD for W1. heldProxy is the persistent PROXY index a view's
    // selection carries across the layout change. For it to still designate a.txt,
    // its ROW must have tracked a.txt's move — exactly what changePersistentIndexList
    // does in onLayoutChanged. QModelIndex equality compares row, column,
    // internalPointer AND model, so this FAILS if heldProxy kept a stale row (the W1
    // bug: a plain source QModelIndex with a frozen .row() would leave heldProxy on
    // a.txt's OLD row — now a different file) and PASSES only when the row was
    // correctly remapped to a.txt's new position. (proxy.filePath() cannot catch
    // this: QFileSystemModel resolves it via the node-pointer chain and ignores the
    // row, so it would pass even under the W1 bug.)
    QVERIFY(heldProxy.isValid());
    const QModelIndex heldNow = heldProxy;
    QCOMPARE(heldNow, fresh);
}

void TestFolderWorkspaceProxy::reRoot_swapsTopNode()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString base = QDir::cleanPath(tmp.path());
    buildTree(base);
    const QString other = base + QStringLiteral("/sub");

    FolderAsWorkspaceFsModel model;
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(base);
    proxy.setRootSourcePath(base);
    QVERIFY(waitForLoaded(&model, base));

    QModelIndex top = proxy.index(0, 0, QModelIndex());
    QCOMPARE(QFileInfo(QDir::cleanPath(proxy.filePath(top))).fileName(),
             QFileInfo(base).fileName());

    // Re-root at a different directory through the single chokepoint.
    model.setRootPath(other);
    proxy.setRootSourcePath(other);
    QVERIFY(waitForLoaded(&model, other));

    QCOMPARE(proxy.rowCount(QModelIndex()), 1);
    top = proxy.index(0, 0, QModelIndex());
    QVERIFY(top.isValid());
    QCOMPARE(QDir::cleanPath(proxy.filePath(top)), QDir::cleanPath(other));
    QCOMPARE(QFileInfo(QDir::cleanPath(proxy.filePath(top))).fileName(),
             QStringLiteral("sub"));
}

void TestFolderWorkspaceProxy::emptyRoot_zeroRows()
{
    QTemporaryDir tmp;
    QVERIFY(tmp.isValid());
    const QString base = QDir::cleanPath(tmp.path());
    buildTree(base);

    FolderAsWorkspaceFsModel model;
    FolderAsWorkspaceProxyModel proxy;
    proxy.setSourceModel(&model);
    model.setRootPath(base);

    // Empty path → invalid root → zero top-level rows.
    proxy.setRootSourcePath(QString());
    QCOMPARE(proxy.rowCount(QModelIndex()), 0);
    QVERIFY(!proxy.hasChildren(QModelIndex()));
    QVERIFY(!proxy.index(0, 0, QModelIndex()).isValid());

    // A nonexistent path → also zero rows.
    proxy.setRootSourcePath(base + QStringLiteral("/does-not-exist-xyz"));
    QCOMPARE(proxy.rowCount(QModelIndex()), 0);
}

// QTEST_MAIN resolves to a QGuiApplication here (Qt6::Gui is linked, Qt6::Widgets
// is not). QFileSystemModel's DecorationRole returns QIcon file icons, which the
// QAbstractItemModelTester queries — those need a GUI application context, so a
// bare QCoreApplication (QTEST_GUILESS_MAIN) is insufficient.
QTEST_MAIN(TestFolderWorkspaceProxy)
#include "test_folder_workspace_proxy.moc"
