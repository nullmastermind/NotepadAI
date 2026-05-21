/*
 * Adapted from kafeg/ptyqt (MIT). Qt6, no GPA_WRAP. See LICENSE.
 */

#include "conptyprocess.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSysInfo>
#include <QThread>
#include <QTimer>

#include <stdlib.h>

bool WindowsContext::init()
{
    if (createPseudoConsole) {
        return true;
    }

    HMODULE kernel32Handle = LoadLibraryExW(L"kernel32.dll", nullptr, 0);
    if (kernel32Handle == nullptr) {
        m_lastError = QStringLiteral("ConPty: unable to load kernel32");
        return false;
    }

    createPseudoConsole = reinterpret_cast<CreatePseudoConsolePtr>(GetProcAddress(kernel32Handle, "CreatePseudoConsole"));
    resizePseudoConsole = reinterpret_cast<ResizePseudoConsolePtr>(GetProcAddress(kernel32Handle, "ResizePseudoConsole"));
    closePseudoConsole  = reinterpret_cast<ClosePseudoConsolePtr>(GetProcAddress(kernel32Handle, "ClosePseudoConsole"));

    if (createPseudoConsole == nullptr || resizePseudoConsole == nullptr || closePseudoConsole == nullptr) {
        m_lastError = QStringLiteral("ConPty: required pseudo-console API not available (Windows 10 1809+ required)");
        return false;
    }

    return true;
}

void PtyBuffer::emitReadyRead()
{
    QTimer::singleShot(1, this, [this]() { emit readyRead(); });
}

void PtyBuffer::onReadyRead()
{
    emit readyRead();
}

HRESULT ConPtyProcess::createPseudoConsoleAndPipes(HPCON *phPC, HANDLE *phPipeIn, HANDLE *phPipeOut, qint16 cols, qint16 rows)
{
    HRESULT hr = E_UNEXPECTED;
    HANDLE hPipePTYIn = INVALID_HANDLE_VALUE;
    HANDLE hPipePTYOut = INVALID_HANDLE_VALUE;

    if (CreatePipe(&hPipePTYIn, phPipeOut, nullptr, 0)
        && CreatePipe(phPipeIn, &hPipePTYOut, nullptr, 0)) {
        COORD coord = { cols, rows };
        hr = m_winContext.createPseudoConsole(coord, hPipePTYIn, hPipePTYOut, 0, phPC);
        if (hPipePTYOut != INVALID_HANDLE_VALUE) CloseHandle(hPipePTYOut);
        if (hPipePTYIn  != INVALID_HANDLE_VALUE) CloseHandle(hPipePTYIn);
    }

    return hr;
}

HRESULT ConPtyProcess::initializeStartupInfoAttachedToPseudoConsole(STARTUPINFOEXW *pStartupInfo, HPCON hPC)
{
    if (!pStartupInfo) {
        return E_UNEXPECTED;
    }

    SIZE_T attrListSize = 0;
    pStartupInfo->StartupInfo.cb = sizeof(STARTUPINFOEXW);

    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrListSize);

    pStartupInfo->lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(malloc(attrListSize));
    if (!pStartupInfo->lpAttributeList) {
        return E_OUTOFMEMORY;
    }

    if (!InitializeProcThreadAttributeList(pStartupInfo->lpAttributeList, 1, 0, &attrListSize)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    if (!UpdateProcThreadAttribute(
            pStartupInfo->lpAttributeList,
            0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            hPC,
            sizeof(HPCON),
            nullptr,
            nullptr)) {
        return HRESULT_FROM_WIN32(GetLastError());
    }

    return S_OK;
}

ConPtyProcess::ConPtyProcess()
    : IPtyProcess()
    , m_ptyHandler(INVALID_HANDLE_VALUE)
    , m_hPipeIn(INVALID_HANDLE_VALUE)
    , m_hPipeOut(INVALID_HANDLE_VALUE)
    , m_readThread(nullptr)
{
}

ConPtyProcess::~ConPtyProcess()
{
    kill();
}

bool ConPtyProcess::startProcess(const QString &shellPath, const QString &workingDir, QStringList environment, qint16 cols, qint16 rows)
{
    if (!isAvailable()) {
        m_lastError = m_winContext.lastError();
        return false;
    }

    if (m_ptyHandler != INVALID_HANDLE_VALUE) {
        return false;
    }

    QFileInfo fi(shellPath);
    if (fi.isRelative() || !QFile::exists(shellPath)) {
        m_lastError = QStringLiteral("ConPty Error: shell file path must be absolute and existing");
        return false;
    }

    m_shellPath = shellPath;
    m_size = QPair<qint16, qint16>(cols, rows);
    Q_UNUSED(environment);

    HRESULT hr = createPseudoConsoleAndPipes(&m_ptyHandler, &m_hPipeIn, &m_hPipeOut, cols, rows);
    if (hr != S_OK) {
        m_lastError = QStringLiteral("ConPty Error: CreatePseudoConsoleAndPipes failed");
        return false;
    }

    STARTUPINFOEXW startupInfo{};
    hr = initializeStartupInfoAttachedToPseudoConsole(&startupInfo, m_ptyHandler);
    if (hr != S_OK) {
        m_lastError = QStringLiteral("ConPty Error: InitializeStartupInfoAttachedToPseudoConsole failed");
        return false;
    }

    PROCESS_INFORMATION piClient{};

    std::wstring cmdLine = shellPath.toStdWString();
    std::vector<wchar_t> cmdMutable(cmdLine.begin(), cmdLine.end());
    cmdMutable.push_back(L'\0');

    LPCWSTR cwd = nullptr;
    std::wstring cwdStr;
    if (!workingDir.isEmpty()) {
        cwdStr = QDir::toNativeSeparators(workingDir).toStdWString();
        cwd = cwdStr.c_str();
    }

    BOOL created = CreateProcessW(
        nullptr,
        cmdMutable.data(),
        nullptr,
        nullptr,
        FALSE,
        EXTENDED_STARTUPINFO_PRESENT,
        nullptr,
        cwd,
        &startupInfo.StartupInfo,
        &piClient);

    if (!created) {
        DWORD err = GetLastError();
        m_lastError = QStringLiteral("ConPty Error: CreateProcess failed (%1)").arg(err);
        return false;
    }
    m_pid = piClient.dwProcessId;

    HANDLE hPipeIn = m_hPipeIn;
    PtyBuffer *bufferPtr = &m_buffer;
    QMutex *mutexPtr = &m_bufferMutex;
    STARTUPINFOEXW localStartup = startupInfo;
    PROCESS_INFORMATION localPi = piClient;

    m_readThread = QThread::create([hPipeIn, bufferPtr, mutexPtr, localPi, localStartup]() mutable {
        const DWORD BUFF_SIZE = 1024;
        char szBuffer[BUFF_SIZE];
        DWORD dwBytesRead = 0;

        for (;;) {
            BOOL fRead = ReadFile(hPipeIn, szBuffer, BUFF_SIZE, &dwBytesRead, nullptr);
            if (fRead && dwBytesRead > 0) {
                QMutexLocker locker(mutexPtr);
                bufferPtr->m_readBuffer.append(szBuffer, dwBytesRead);
                bufferPtr->emitReadyRead();
            }

            if (QThread::currentThread()->isInterruptionRequested()) {
                break;
            }
            if (!fRead || dwBytesRead == 0) {
                if (QThread::currentThread()->isInterruptionRequested()) break;
                QThread::msleep(10);
            }
        }

        CloseHandle(localPi.hThread);
        CloseHandle(localPi.hProcess);
        if (localStartup.lpAttributeList) {
            DeleteProcThreadAttributeList(localStartup.lpAttributeList);
            free(localStartup.lpAttributeList);
        }
    });

    m_readThread->start();

    return true;
}

bool ConPtyProcess::resize(qint16 cols, qint16 rows)
{
    if (m_ptyHandler == INVALID_HANDLE_VALUE || m_ptyHandler == nullptr) {
        return false;
    }
    COORD coord = { cols, rows };
    bool res = SUCCEEDED(m_winContext.resizePseudoConsole(m_ptyHandler, coord));
    if (res) {
        m_size = QPair<qint16, qint16>(cols, rows);
    }
    return res;
}

bool ConPtyProcess::kill()
{
    if (m_ptyHandler == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (m_winContext.closePseudoConsole) {
        m_winContext.closePseudoConsole(m_ptyHandler);
    }

    if (m_readThread) {
        m_readThread->requestInterruption();
        m_readThread->quit();
        if (!m_readThread->wait(500)) {
            m_readThread->terminate();
            m_readThread->wait(100);
        }
        delete m_readThread;
        m_readThread = nullptr;
    }

    if (m_hPipeOut != INVALID_HANDLE_VALUE) CloseHandle(m_hPipeOut);
    if (m_hPipeIn  != INVALID_HANDLE_VALUE) CloseHandle(m_hPipeIn);
    m_pid = 0;
    m_ptyHandler = INVALID_HANDLE_VALUE;
    m_hPipeIn = INVALID_HANDLE_VALUE;
    m_hPipeOut = INVALID_HANDLE_VALUE;
    return true;
}

IPtyProcess::PtyType ConPtyProcess::type() const
{
    return PtyType::ConPty;
}

QString ConPtyProcess::dumpDebugInfo()
{
    return QStringLiteral("ConPty PID: %1, Cols: %2, Rows: %3")
        .arg(m_pid).arg(m_size.first).arg(m_size.second);
}

QIODevice *ConPtyProcess::notifier()
{
    return &m_buffer;
}

QByteArray ConPtyProcess::readAll()
{
    QMutexLocker locker(&m_bufferMutex);
    QByteArray out = m_buffer.m_readBuffer;
    m_buffer.m_readBuffer.clear();
    return out;
}

qint64 ConPtyProcess::write(const QByteArray &byteArray)
{
    DWORD dwBytesWritten = 0;
    BOOL ok = WriteFile(m_hPipeOut, byteArray.constData(),
                        static_cast<DWORD>(byteArray.size()),
                        &dwBytesWritten, nullptr);
    if (!ok) {
        m_lastError = QStringLiteral("ConPty Error: WriteFile failed (%1)").arg(GetLastError());
        return -1;
    }
    return static_cast<qint64>(dwBytesWritten);
}

bool ConPtyProcess::isAvailable()
{
#ifdef TOO_OLD_WINSDK
    return false;
#else
    // kernelVersion() returns e.g. "10.0.22621.2861". Index 2 is the OS build
    // number; parts.last() would be the UBR (Update Build Revision, typically
    // 3-4 digits) which is meaningless for ConPTY gating.
    const QString kernelVersion = QSysInfo::kernelVersion();
    const QStringList parts = kernelVersion.split(QLatin1Char('.'));
    if (parts.size() >= 3) {
        bool ok = false;
        const qint32 buildNumber = parts.at(2).toInt(&ok);
        if (ok && buildNumber > 0 && buildNumber < CONPTY_MINIMAL_WINDOWS_VERSION) {
            return false;
        }
    }
    return m_winContext.init();
#endif
}

void ConPtyProcess::moveToThread(QThread *targetThread)
{
    Q_UNUSED(targetThread);
}
