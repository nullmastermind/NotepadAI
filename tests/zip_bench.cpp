/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Local benchmark for folder-zip download: walk + pack, no GUI/SSH.
 * Usage: zip_bench [root] [out.zip]
 * Not registered with ctest — run manually after `just test` builds it.
 */

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <cstdio>

#include "ZipArchive.h"
#include "ZipIgnoreWalk.h"

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    const QStringList args = app.arguments();
    QString root = args.size() > 1 ? args.at(1) : QDir::currentPath();
    root = QDir::cleanPath(root);
    QString out = args.size() > 2
        ? args.at(2)
        : QDir::temp().filePath(QStringLiteral("zip_bench_notepad_ade.zip"));

    QElapsedTimer walkTimer;
    walkTimer.start();
    const auto files = ZipIgnoreWalk::walkLocalFolder(root, root);
    const qint64 walkMs = walkTimer.elapsed();

    QElapsedTimer packTimer;
    packTimer.start();
    ZipWriter writer;
    if (!writer.open(out)) {
        qCritical("open failed: %s", qPrintable(writer.errorString()));
        return 1;
    }
    int okCount = 0;
    int failCount = 0;
    qint64 totalBytes = 0;
    for (const auto &f : files) {
        totalBytes += QFileInfo(f.absPath).size();
        if (writer.addFile(f.absPath, f.entryName)) {
            ++okCount;
        } else {
            ++failCount;
            qWarning("add failed: %s (%s)", qPrintable(f.entryName),
                     qPrintable(writer.errorString()));
        }
    }
    if (!writer.finish()) {
        qCritical("finish failed: %s", qPrintable(writer.errorString()));
        return 1;
    }
    const qint64 packMs = packTimer.elapsed();
    const qint64 outSize = QFileInfo(out).size();
    QFile::remove(out);

    std::printf("root=%s\n", qPrintable(root));
    std::printf("files=%d ok=%d fail=%d source_bytes=%lld\n", int(files.size()), okCount, failCount,
                static_cast<long long>(totalBytes));
    std::printf("walk_ms=%lld\n", static_cast<long long>(walkMs));
    std::printf("pack_ms=%lld\n", static_cast<long long>(packMs));
    std::printf("total_ms=%lld\n", static_cast<long long>(walkMs + packMs));
    std::printf("zip_bytes=%lld\n", static_cast<long long>(outSize));
    std::fflush(stdout);
    return failCount > 0 ? 2 : 0;
}
