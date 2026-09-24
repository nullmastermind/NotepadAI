/*
 * Adapted from kafeg/ptyqt (MIT). Trimmed for NotepadADE: Qt6 only,
 * ConPTY + Unix forkpty backends only (no WinPTY), no PCRE2/OpenSSL.
 * See LICENSE.
 */

#ifndef IPTYPROCESS_H
#define IPTYPROCESS_H

#include <QObject>
#include <QString>
#include <QStringList>

#define CONPTY_MINIMAL_WINDOWS_VERSION 17763

class QIODevice;
class QThread;

class IPtyProcess : public QObject
{
    Q_OBJECT
public:
    enum PtyType
    {
        UnixPty = 0,
        WinPty  = 1,
        ConPty  = 2,
        AutoPty = 3
    };

    IPtyProcess()
        : m_pid(0)
    {}
    ~IPtyProcess() override = default;

    virtual bool startProcess(const QString &shellPath, const QString &workingDir, QStringList environment, qint16 cols, qint16 rows) = 0;
    virtual bool resize(qint16 cols, qint16 rows) = 0;
    virtual bool kill() = 0;
    virtual PtyType type() const = 0;
    virtual QString dumpDebugInfo() = 0;
    virtual QIODevice *notifier() = 0;
    virtual QByteArray readAll() = 0;
    virtual qint64 write(const QByteArray &byteArray) = 0;
    virtual bool isAvailable() = 0;
    virtual void moveToThread(QThread *targetThread) = 0;
    qint64 pid() const { return m_pid; }
    QPair<qint16, qint16> size() const { return m_size; }
    QString lastError() const { return m_lastError; }

    // 0 = uncapped. Windows: job-wide commit ceiling for the shell and every
    // descendant. POSIX: RLIMIT_AS inherited by the shell and its children
    // (per process, not a tree sum). Ignored by the SSH backend.
    void setChildCommitLimit(quint64 bytes) { m_childCommitLimit = bytes; }
    quint64 childCommitLimit() const { return m_childCommitLimit; }

signals:
    void finished(int exitCode);
    // A process in this terminal's tree was stopped for exceeding the commit cap.
    void memoryLimitExceeded();

protected:
    QString m_shellPath;
    QString m_lastError;
    qint64 m_pid;
    QPair<qint16, qint16> m_size;
    quint64 m_childCommitLimit = 0;
};

#endif
