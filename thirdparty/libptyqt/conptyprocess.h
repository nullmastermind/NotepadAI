/*
 * Adapted from kafeg/ptyqt (MIT). Qt6, no GPA_WRAP. See LICENSE.
 */

#ifndef CONPTYPROCESS_H
#define CONPTYPROCESS_H

#include "iptyprocess.h"

#include <QIODevice>
#include <QMutex>
#include <QThread>

#include <windows.h>
#include <process.h>

#ifndef PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE
#define PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE \
    ProcThreadAttributeValue(22, FALSE, TRUE, FALSE)
typedef VOID* HPCON;
#define TOO_OLD_WINSDK
#endif

class WindowsContext
{
public:
    typedef HRESULT (WINAPI *CreatePseudoConsolePtr)(COORD size, HANDLE hInput, HANDLE hOutput, DWORD dwFlags, HPCON *phPC);
    typedef HRESULT (WINAPI *ResizePseudoConsolePtr)(HPCON hPC, COORD size);
    typedef VOID    (WINAPI *ClosePseudoConsolePtr)(HPCON hPC);

    WindowsContext()
        : createPseudoConsole(nullptr)
        , resizePseudoConsole(nullptr)
        , closePseudoConsole(nullptr)
    {}

    bool init();
    QString lastError() const { return m_lastError; }

    CreatePseudoConsolePtr createPseudoConsole;
    ResizePseudoConsolePtr resizePseudoConsole;
    ClosePseudoConsolePtr  closePseudoConsole;

private:
    QString m_lastError;
};

class PtyBuffer : public QIODevice
{
    friend class ConPtyProcess;
    Q_OBJECT
public:
    PtyBuffer() = default;
    ~PtyBuffer() override = default;

    qint64 readData(char *data, qint64 maxlen) override { Q_UNUSED(data); Q_UNUSED(maxlen); return 0; }
    qint64 writeData(const char *data, qint64 len) override { Q_UNUSED(data); Q_UNUSED(len); return 0; }

    bool   isSequential() const override { return true; }
    qint64 bytesAvailable() const override { return m_readBuffer.size(); }
    qint64 size() const override { return m_readBuffer.size(); }

    void emitReadyRead();

private slots:
    void onReadyRead();

public:
    QByteArray m_readBuffer;
};

class ConPtyProcess : public IPtyProcess
{
    Q_OBJECT
public:
    ConPtyProcess();
    ~ConPtyProcess() override;

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

signals:
    void requestInterruption();

private:
    HRESULT createPseudoConsoleAndPipes(HPCON *phPC, HANDLE *phPipeIn, HANDLE *phPipeOut, qint16 cols, qint16 rows);
    HRESULT initializeStartupInfoAttachedToPseudoConsole(STARTUPINFOEXW *pStartupInfo, HPCON hPC);

    WindowsContext m_winContext;
    HPCON  m_ptyHandler;
    HANDLE m_hPipeIn;
    HANDLE m_hPipeOut;

    QThread *m_readThread;
    QMutex m_bufferMutex;
    PtyBuffer m_buffer;
};

#endif
