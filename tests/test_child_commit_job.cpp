/*
 * Windows job commit ceiling used by local terminals.
 * A child assigned to a job whose limit is just above its current commit must
 * fail a further allocation that would cross that ceiling.
 */

#include "commitjob.h"

#include <QtTest>
#include <QCoreApplication>

#include <cstring>
#include <string>
#include <vector>

#ifdef Q_OS_WIN
#include <windows.h>
#include <psapi.h>
#endif

namespace {

#ifdef Q_OS_WIN
int commitJobChild()
{
    constexpr SIZE_T kAlloc = 64ull * 1024ull * 1024ull;
    void *block = VirtualAlloc(nullptr, kAlloc, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);
    if (!block) {
        const DWORD err = GetLastError();
        if (err == ERROR_NOT_ENOUGH_MEMORY || err == ERROR_COMMITMENT_LIMIT) {
            return 3;
        }
        return 4;
    }
    VirtualFree(block, 0, MEM_RELEASE);
    return 0;
}

bool spawnChild(bool assignJob, PROCESS_INFORMATION *pi, HANDLE *jobOut, QString *err)
{
    auto fail = [&](const QString &why) {
        if (pi->hThread) {
            TerminateProcess(pi->hProcess, 1);
            CloseHandle(pi->hThread);
            CloseHandle(pi->hProcess);
            pi->hThread = nullptr;
            pi->hProcess = nullptr;
        }
        *err = why;
        return false;
    };

    wchar_t exe[MAX_PATH] = {};
    if (GetModuleFileNameW(nullptr, exe, MAX_PATH) == 0) {
        *err = QStringLiteral("GetModuleFileNameW failed");
        return false;
    }
    std::wstring cmd = L"\"";
    cmd += exe;
    cmd += L"\" --commit-job-child";
    std::vector<wchar_t> mutableCmd(cmd.begin(), cmd.end());
    mutableCmd.push_back(L'\0');

    STARTUPINFOW si{};
    si.cb = sizeof(si);
    if (!CreateProcessW(nullptr, mutableCmd.data(), nullptr, nullptr, FALSE,
                        CREATE_SUSPENDED | CREATE_NO_WINDOW, nullptr, nullptr, &si, pi)) {
        *err = QStringLiteral("CreateProcess failed (%1)").arg(GetLastError());
        return false;
    }

    if (!assignJob) {
        return true;
    }

    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (!GetProcessMemoryInfo(pi->hProcess,
                              reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(&counters),
                              sizeof(counters))) {
        return fail(QStringLiteral("GetProcessMemoryInfo failed (%1)").arg(GetLastError()));
    }
    // 8 MB of headroom above current commit. The child then asks for 64 MB.
    const std::uint64_t limit = static_cast<std::uint64_t>(counters.PrivateUsage) + (8ull << 20);
    HANDLE job = static_cast<HANDLE>(createCommitLimitedJob(limit));
    if (!job) {
        return fail(QStringLiteral("createCommitLimitedJob failed"));
    }
    if (!assignProcessToCommitJob(job, pi->hProcess)) {
        const DWORD assignErr = GetLastError();
        CloseHandle(job);
        return fail(QStringLiteral("AssignProcessToJobObject failed (%1)").arg(assignErr));
    }
    *jobOut = job;
    return true;
}

int waitChild(PROCESS_INFORMATION *pi, HANDLE job)
{
    ResumeThread(pi->hThread);
    const DWORD wait = WaitForSingleObject(pi->hProcess, 15000);
    DWORD code = 0xFFFFFFFFu;
    if (wait == WAIT_OBJECT_0) {
        GetExitCodeProcess(pi->hProcess, &code);
    }
    CloseHandle(pi->hThread);
    CloseHandle(pi->hProcess);
    if (job) {
        CloseHandle(job);
    }
    if (wait != WAIT_OBJECT_0) {
        return -1;
    }
    return static_cast<int>(code);
}
#endif

} // namespace

class TestChildCommitJob : public QObject
{
    Q_OBJECT
private slots:
    void zeroLimitReturnsNull();
    void limitIsStoredNotReserved();
    void childAllocationPastCeilingFails();
};

void TestChildCommitJob::zeroLimitReturnsNull()
{
#ifdef Q_OS_WIN
    QCOMPARE(createCommitLimitedJob(0), nullptr);
#else
    QSKIP("Windows job objects only");
#endif
}

void TestChildCommitJob::limitIsStoredNotReserved()
{
#ifdef Q_OS_WIN
    constexpr std::uint64_t kLimit = 16ull << 30;
    HANDLE job = static_cast<HANDLE>(createCommitLimitedJob(kLimit));
    QVERIFY(job);

    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    QVERIFY(QueryInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info), nullptr));
    QCOMPARE(info.JobMemoryLimit, kLimit);
    QVERIFY(info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_JOB_MEMORY);
    QVERIFY(info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE);
    QVERIFY(!(info.BasicLimitInformation.LimitFlags & JOB_OBJECT_LIMIT_BREAKAWAY_OK));
    CloseHandle(job);
#else
    QSKIP("Windows job objects only");
#endif
}

void TestChildCommitJob::childAllocationPastCeilingFails()
{
#ifdef Q_OS_WIN
    PROCESS_INFORMATION control{};
    HANDLE noJob = nullptr;
    QString err;
    QVERIFY2(spawnChild(false, &control, &noJob, &err), qPrintable(err));
    const int controlCode = waitChild(&control, noJob);
    if (controlCode == 3) {
        QSKIP("Machine cannot commit 64 MB; cannot distinguish a job cap from system OOM");
    }
    QCOMPARE(controlCode, 0);

    PROCESS_INFORMATION capped{};
    HANDLE job = nullptr;
    QVERIFY2(spawnChild(true, &capped, &job, &err), qPrintable(err));
    const int cappedCode = waitChild(&capped, job);
    QCOMPARE(cappedCode, 3);
#else
    QSKIP("Windows job objects only");
#endif
}

int main(int argc, char **argv)
{
#ifdef Q_OS_WIN
    if (argc >= 2 && std::strcmp(argv[1], "--commit-job-child") == 0) {
        return commitJobChild();
    }
#endif
    QCoreApplication app(argc, argv);
    TestChildCommitJob tc;
    return QTest::qExec(&tc, argc, argv);
}

#include "test_child_commit_job.moc"
