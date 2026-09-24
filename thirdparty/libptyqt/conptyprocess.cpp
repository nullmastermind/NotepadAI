/*
 * Adapted from kafeg/ptyqt (MIT). Qt6, no GPA_WRAP. See LICENSE.
 */

#include "conptyprocess.h"
#include "commitjob.h"

#include <QCoreApplication>
#include <QDebug>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMutexLocker>
#include <QSysInfo>
#include <QThread>
#include <QTimer>

#include <map>
#include <stdlib.h>
#include <string>

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

HRESULT ConPtyProcess::initializeStartupInfoAttachedToPseudoConsole(STARTUPINFOEXW *pStartupInfo, HPCON hPC,
                                                                     HANDLE *jobList, DWORD jobCount)
{
    if (!pStartupInfo) {
        return E_UNEXPECTED;
    }

    const DWORD attrCount = jobCount > 0 ? 2u : 1u;
    SIZE_T attrListSize = 0;
    pStartupInfo->StartupInfo.cb = sizeof(STARTUPINFOEXW);

    InitializeProcThreadAttributeList(nullptr, attrCount, 0, &attrListSize);

    pStartupInfo->lpAttributeList = reinterpret_cast<LPPROC_THREAD_ATTRIBUTE_LIST>(malloc(attrListSize));
    if (!pStartupInfo->lpAttributeList) {
        return E_OUTOFMEMORY;
    }

    if (!InitializeProcThreadAttributeList(pStartupInfo->lpAttributeList, attrCount, 0, &attrListSize)) {
        const DWORD err = GetLastError();
        free(pStartupInfo->lpAttributeList);
        pStartupInfo->lpAttributeList = nullptr;
        return HRESULT_FROM_WIN32(err);
    }

    if (!UpdateProcThreadAttribute(
            pStartupInfo->lpAttributeList,
            0,
            PROC_THREAD_ATTRIBUTE_PSEUDOCONSOLE,
            hPC,
            sizeof(HPCON),
            nullptr,
            nullptr)) {
        const DWORD err = GetLastError();
        DeleteProcThreadAttributeList(pStartupInfo->lpAttributeList);
        free(pStartupInfo->lpAttributeList);
        pStartupInfo->lpAttributeList = nullptr;
        return HRESULT_FROM_WIN32(err);
    }

    if (jobCount > 0 && jobList) {
        if (!UpdateProcThreadAttribute(
                pStartupInfo->lpAttributeList,
                0,
                PROC_THREAD_ATTRIBUTE_JOB_LIST,
                jobList,
                sizeof(HANDLE) * jobCount,
                nullptr,
                nullptr)) {
            const DWORD err = GetLastError();
            DeleteProcThreadAttributeList(pStartupInfo->lpAttributeList);
            free(pStartupInfo->lpAttributeList);
            pStartupInfo->lpAttributeList = nullptr;
            return HRESULT_FROM_WIN32(err);
        }
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

    // Build environment block if custom env vars are provided.
    std::vector<wchar_t> envBlock;
    LPVOID lpEnvironment = nullptr;
    DWORD extraFlags = 0;
    if (!environment.isEmpty()) {
        // Parse parent environment into a case-insensitive sorted map.
        struct WCILess {
            bool operator()(const std::wstring &a, const std::wstring &b) const {
                int r = CompareStringOrdinal(a.c_str(), static_cast<int>(a.size()),
                                             b.c_str(), static_cast<int>(b.size()), TRUE);
                return r == CSTR_LESS_THAN;
            }
        };
        std::map<std::wstring, std::wstring, WCILess> envMap;

        LPWCH parentBlock = GetEnvironmentStringsW();
        if (parentBlock) {
            for (LPCWSTR p = parentBlock; *p; p += wcslen(p) + 1) {
                std::wstring entry(p);
                // =X: pseudo-vars have '=' at pos 0; split on first '=' after that.
                size_t eq = entry.find(L'=', (entry[0] == L'=') ? 1 : 0);
                if (eq != std::wstring::npos) {
                    envMap[entry.substr(0, eq)] = entry.substr(eq + 1);
                }
            }
            FreeEnvironmentStringsW(parentBlock);
        }

        // Merge custom environment (override or unset if value is empty).
        for (const QString &kv : environment) {
            const int eq = kv.indexOf(QLatin1Char('='));
            if (eq > 0) {
                std::wstring key = kv.left(eq).toStdWString();
                std::wstring val = kv.mid(eq + 1).toStdWString();
                envMap[key] = val;
            }
        }

        // Serialize to flat Unicode block: KEY=VALUE\0 ... \0\0
        for (const auto &[k, v] : envMap) {
            envBlock.insert(envBlock.end(), k.begin(), k.end());
            envBlock.push_back(L'=');
            envBlock.insert(envBlock.end(), v.begin(), v.end());
            envBlock.push_back(L'\0');
        }
        envBlock.push_back(L'\0');

        lpEnvironment = envBlock.data();
        extraFlags = CREATE_UNICODE_ENVIRONMENT;
    }

    HRESULT hr = createPseudoConsoleAndPipes(&m_ptyHandler, &m_hPipeIn, &m_hPipeOut, cols, rows);
    if (hr != S_OK) {
        m_lastError = QStringLiteral("ConPty Error: CreatePseudoConsoleAndPipes failed");
        return false;
    }

    if (m_childCommitLimit > 0) {
        m_job = static_cast<HANDLE>(createCommitLimitedJob(m_childCommitLimit));
        if (!m_job) {
            qWarning("Terminal: could not create memory-limit job; launching uncapped");
        }
    }

    std::wstring cmdLine = shellPath.toStdWString();
    std::vector<wchar_t> cmdMutable(cmdLine.begin(), cmdLine.end());
    cmdMutable.push_back(L'\0');

    LPCWSTR cwd = nullptr;
    std::wstring cwdStr;
    if (!workingDir.isEmpty()) {
        cwdStr = QDir::toNativeSeparators(workingDir).toStdWString();
        cwd = cwdStr.c_str();
    }

    // jobList must outlive CreateProcess; the attribute list holds the pointer.
    HANDLE jobList[1] = { m_job };

    auto spawn = [&](HANDLE job, PROCESS_INFORMATION *pi, DWORD *errOut) -> bool {
        STARTUPINFOEXW startupInfo{};
        const HRESULT hr = initializeStartupInfoAttachedToPseudoConsole(
            &startupInfo, m_ptyHandler, job ? jobList : nullptr, job ? 1u : 0u);
        if (hr != S_OK) {
            if (errOut) {
                *errOut = HRESULT_CODE(hr);
            }
            m_lastError = QStringLiteral("ConPty Error: InitializeStartupInfoAttachedToPseudoConsole failed");
            return false;
        }

        const BOOL created = CreateProcessW(
            nullptr,
            cmdMutable.data(),
            nullptr,
            nullptr,
            FALSE,
            EXTENDED_STARTUPINFO_PRESENT | extraFlags,
            lpEnvironment,
            cwd,
            &startupInfo.StartupInfo,
            pi);

        const DWORD err = created ? 0 : GetLastError();
        if (startupInfo.lpAttributeList) {
            DeleteProcThreadAttributeList(startupInfo.lpAttributeList);
            free(startupInfo.lpAttributeList);
        }
        if (!created) {
            if (errOut) {
                *errOut = err;
            }
            m_lastError = QStringLiteral("ConPty Error: CreateProcess failed (%1)").arg(err);
            return false;
        }
        return true;
    };

    PROCESS_INFORMATION piClient{};
    DWORD spawnErr = 0;
    bool spawned = false;
    if (m_job) {
        spawned = spawn(m_job, &piClient, &spawnErr);
        if (!spawned) {
            // ConPTY + JOB_LIST is rejected on some builds. Launch, then assign.
            // The shell is idle until the first prompt, so the assign race does
            // not let a grandchild escape before the cap is in place.
            qWarning("Terminal: CreateProcess with job list failed (%lu); assigning after spawn", spawnErr);
            spawned = spawn(nullptr, &piClient, &spawnErr);
            if (spawned && !assignProcessToCommitJob(m_job, piClient.hProcess)) {
                qWarning("Terminal: memory limit not applied (%lu)", GetLastError());
                CloseHandle(m_job);
                m_job = nullptr;
            }
        }
    } else {
        spawned = spawn(nullptr, &piClient, &spawnErr);
    }

    if (!spawned) {
        if (m_job) {
            CloseHandle(m_job);
            m_job = nullptr;
        }
        return false;
    }
    m_pid = piClient.dwProcessId;
    m_processHandle = piClient.hProcess;

    HANDLE hPipeIn = m_hPipeIn;
    PtyBuffer *bufferPtr = &m_buffer;
    QMutex *mutexPtr = &m_bufferMutex;
    HANDLE procHandle = piClient.hProcess;
    IPtyProcess *self = this;

    CloseHandle(piClient.hThread);

    if (m_job) {
        startJobWatch();
    }

    m_readThread = QThread::create([hPipeIn, bufferPtr, mutexPtr, procHandle, self]() {
        const DWORD BUFF_SIZE = 4096;
        char szBuffer[BUFF_SIZE];
        DWORD dwBytesRead = 0;

        for (;;) {
            BOOL fRead = ReadFile(hPipeIn, szBuffer, BUFF_SIZE, &dwBytesRead, nullptr);
            if (fRead && dwBytesRead > 0) {
                QMutexLocker locker(mutexPtr);
                bufferPtr->m_readBuffer.append(szBuffer, dwBytesRead);
                bufferPtr->emitReadyRead();
                continue;
            }
            break;
        }

        DWORD exitCode = 0;
        WaitForSingleObject(procHandle, 3000);
        GetExitCodeProcess(procHandle, &exitCode);
        QMetaObject::invokeMethod(self, [self, exitCode]() {
            emit self->finished(static_cast<int>(exitCode));
        }, Qt::QueuedConnection);
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
    stopJobWatch();
    // Last handle: KILL_ON_JOB_CLOSE reaps the shell and every descendant.
    if (m_job) {
        CloseHandle(m_job);
        m_job = nullptr;
    }

    if (m_ptyHandler == INVALID_HANDLE_VALUE) {
        return false;
    }

    if (m_processHandle != INVALID_HANDLE_VALUE) {
        TerminateProcess(m_processHandle, 1);
    }

    if (m_winContext.closePseudoConsole) {
        m_winContext.closePseudoConsole(m_ptyHandler);
    }

    if (m_readThread) {
        if (!m_readThread->wait(2000)) {
            m_readThread->terminate();
            m_readThread->wait(100);
        }
        delete m_readThread;
        m_readThread = nullptr;
    }

    if (m_processHandle != INVALID_HANDLE_VALUE) {
        CloseHandle(m_processHandle);
        m_processHandle = INVALID_HANDLE_VALUE;
    }
    if (m_hPipeOut != INVALID_HANDLE_VALUE) CloseHandle(m_hPipeOut);
    if (m_hPipeIn  != INVALID_HANDLE_VALUE) CloseHandle(m_hPipeIn);
    m_pid = 0;
    m_ptyHandler = INVALID_HANDLE_VALUE;
    m_hPipeIn = INVALID_HANDLE_VALUE;
    m_hPipeOut = INVALID_HANDLE_VALUE;
    return true;
}

void ConPtyProcess::deliverMemoryLimitNotice()
{
    if (m_limitNoticeSent) {
        return;
    }
    m_limitNoticeSent = true;
    emit memoryLimitExceeded();
}

void ConPtyProcess::startJobWatch()
{
    if (!m_job || m_jobWatchThread) {
        return;
    }

    m_jobPort = CreateIoCompletionPort(INVALID_HANDLE_VALUE, nullptr, 0, 1);
    if (!m_jobPort) {
        return;
    }

    JOBOBJECT_ASSOCIATE_COMPLETION_PORT assoc{};
    assoc.CompletionKey = m_job;
    assoc.CompletionPort = m_jobPort;
    if (!SetInformationJobObject(m_job, JobObjectAssociateCompletionPortInformation, &assoc, sizeof(assoc))) {
        CloseHandle(m_jobPort);
        m_jobPort = nullptr;
        return;
    }

    HANDLE port = m_jobPort;
    HANDLE job = m_job;
    ConPtyProcess *self = this;
    void (ConPtyProcess::*notify)() = &ConPtyProcess::deliverMemoryLimitNotice;
    m_jobWatchThread = QThread::create([port, job, self, notify]() {
        for (;;) {
            DWORD msg = 0;
            ULONG_PTR key = 0;
            LPOVERLAPPED ov = nullptr;
            if (!GetQueuedCompletionStatus(port, &msg, &key, &ov, INFINITE) && ov == nullptr) {
                break;
            }
            if (key == 0) {
                break;
            }
            if (msg != JOB_OBJECT_MSG_JOB_MEMORY_LIMIT && msg != JOB_OBJECT_MSG_PROCESS_MEMORY_LIMIT) {
                continue;
            }
            const DWORD pid = static_cast<DWORD>(reinterpret_cast<uintptr_t>(ov));
            if (pid != 0) {
                HANDLE ph = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
                if (ph) {
                    TerminateProcess(ph, 1);
                    CloseHandle(ph);
                } else if (GetLastError() != ERROR_INVALID_PARAMETER) {
                    // Still alive but not terminable from here. Drop the tree
                    // rather than leave a process pinned at the commit ceiling.
                    TerminateJobObject(job, 1);
                }
            }
            QMetaObject::invokeMethod(self, notify, Qt::QueuedConnection);
        }
    });
    m_jobWatchThread->start();
}

void ConPtyProcess::stopJobWatch()
{
    if (m_jobPort) {
        PostQueuedCompletionStatus(m_jobPort, 0, 0, nullptr);
    }
    if (m_jobWatchThread) {
        if (!m_jobWatchThread->wait(2000)) {
            m_jobWatchThread->terminate();
            m_jobWatchThread->wait(100);
        }
        delete m_jobWatchThread;
        m_jobWatchThread = nullptr;
    }
    if (m_jobPort) {
        CloseHandle(m_jobPort);
        m_jobPort = nullptr;
    }
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
}

void ConPtyProcess::moveToThread(QThread *targetThread)
{
    Q_UNUSED(targetThread);
}
