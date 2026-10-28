/*
 * This file is part of Notepad Next.
 * Copyright 2026 NotepadAI contributors
 *
 * Notepad Next is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 */

// The controllability rule for pickers (docs/adr/0006): every file, folder,
// font and colour picker goes through control::picker so an external agent's
// direct command can supply its value. A direct QFileDialog::get* call would
// leave that picker unreachable.

#include <QtTest>
#include <QDirIterator>
#include <QRegularExpression>

class TestControlPickerSeam : public QObject
{
    Q_OBJECT

private slots:
    void noDirectPickerCalls();
};

void TestControlPickerSeam::noDirectPickerCalls()
{
    const QString srcDir = QStringLiteral(NOTEPADAI_SRC_DIR);
    QVERIFY2(QDir(srcDir).exists(), qPrintable(srcDir));
    static const QRegularExpression forbidden(
        QStringLiteral(R"(\bQFileDialog::get\w+\s*\(|\bQFontDialog::getFont\s*\(|\bQColorDialog::getColor\s*\()"));

    QStringList offenders;
    QDirIterator it(srcDir, {QStringLiteral("*.cpp"), QStringLiteral("*.h"), QStringLiteral("*.mm")}, QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (path.endsWith(QLatin1String("control/ControlPicker.cpp")))
            continue;
        QFile f(path);
        QVERIFY(f.open(QIODevice::ReadOnly | QIODevice::Text));
        int line = 0;
        while (!f.atEnd()) {
            ++line;
            const QString text = QString::fromUtf8(f.readLine());
            if (forbidden.match(text).hasMatch())
                offenders.append(QStringLiteral("%1:%2: %3").arg(QDir(srcDir).relativeFilePath(path)).arg(line).arg(text.trimmed()));
        }
    }
    QVERIFY2(offenders.isEmpty(),
             qPrintable(QStringLiteral("use control::picker instead:\n") + offenders.join(QLatin1Char('\n'))));
}

QTEST_APPLESS_MAIN(TestControlPickerSeam)
#include "test_control_picker_seam.moc"
