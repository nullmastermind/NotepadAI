/*
 * Adapted from kafeg/ptyqt (MIT). Qt6 rewrite using forkpty (POSIX).
 * See LICENSE.
 */

#ifndef UNIXPTYPROCESS_H
#define UNIXPTYPROCESS_H

#include "iptyprocess.h"

#include <QByteArray>
#include <QIODevice>
#include <QSocketNotifier>

#include <sys/types.h>

class UnixPtyNotifier : public QIODevice
{
    Q_OBJECT
public:
    explicit UnixPtyNotifier(QObject *parent = nullptr) : QIODevice(parent) { open(QIODevice::ReadOnly); }
    ~UnixPtyNotifier() override = default;
    qint64 readData(char *, qint64) override { return 0; }
    qint64 writeData(const char *, qint64) override { return 0; }
    bool isSequential() const override { return true; }
    void emitReadyRead() { emit readyRead(); }
};

class UnixPtyProcess : public IPtyProcess
{
    Q_OBJECT
public:
    UnixPtyProcess();
    ~UnixPtyProcess() override;

    bool startProcess(const QString &shellPath, const QString &workingDir, QStringList environment, qint16 cols, qint16 rows) override;
    bool resize(qint16 cols, qint16 rows) override;
    bool kill() override;
    PtyType type() const override;
    QString dumpDebugInfo() override;
    QIODevice *notifier() override;
    QByteArray readAll() override;
    qint64 write(const QByteArray &byteArray) override;
    bool isAvailable() override;
    void moveToThread(QThread *targetThread) override;

private slots:
    void onMasterReadable();

private:
    int   m_masterFd;
    pid_t m_childPid;
    QSocketNotifier *m_readNotifier;
    QByteArray m_readBuffer;
    UnixPtyNotifier m_notifier;
};

#endif
