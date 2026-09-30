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

#include "TerminalProcessStats.h"

#include <QCoreApplication>

#include <QHash>
#include <QThread>
#include <QVarLengthArray>
#include <QtGlobal>

#include <chrono>
#include <cstddef>
#include <cstring>



#ifdef Q_OS_WIN
#  ifndef WIN32_LEAN_AND_MEAN
#    define WIN32_LEAN_AND_MEAN
#  endif
#  include <windows.h>
#elif defined(Q_OS_LINUX)
#  include <dirent.h>
#  include <stdio.h>
#  include <stdlib.h>
#  include <unistd.h>
#elif defined(Q_OS_MACOS)
#  include <libproc.h>
#  include <mach/mach.h>
#  include <sys/proc_info.h>
#  include <sys/sysctl.h>
#endif

TerminalProcessStats::TreeStats TerminalProcessStats::aggregateTree(
    const ProcessSample *samples,
    int sampleCount,
    quint32 rootPid,
    const TreeCpuState *prev,
    quint64 nowWall100ns,
    int logicalCpus,
    TreeCpuState *outState)
{
    TreeStats result;
    if (!samples || sampleCount <= 0 || rootPid == 0)
        return result;

    QHash<quint32, int> index;
    index.reserve(sampleCount);
    for (int i = 0; i < sampleCount; ++i) {
        if (samples[i].pid != 0)
            index.insert(samples[i].pid, i);
    }
    const auto rootIt = index.constFind(rootPid);
    if (rootIt == index.cend())
        return result;

    QVarLengthArray<char, 256> inTree(sampleCount);
    std::memset(inTree.data(), 0, static_cast<size_t>(sampleCount));

    inTree[*rootIt] = 1;

    bool grew = true;
    while (grew) {
        grew = false;
        for (int i = 0; i < sampleCount; ++i) {
            if (inTree[i])
                continue;
            const quint32 pp = samples[i].parentPid;
            if (pp == 0)
                continue;
            const auto pit = index.constFind(pp);
            if (pit != index.cend() && inTree[*pit]) {
                inTree[i] = 1;
                grew = true;
            }
        }
    }

    quint64 cpuSum = 0;
    quint64 rssSum = 0;
    for (int i = 0; i < sampleCount; ++i) {
        if (!inTree[i])
            continue;
        cpuSum += samples[i].cpuTime100ns;
        rssSum += samples[i].rssBytes;
    }

    result.valid = true;
    result.rssBytes = rssSum;

    const int cpus = logicalCpus > 0 ? logicalCpus : 1;
    const bool haveDelta = prev
        && prev->rootPid == rootPid
        && prev->wall100ns > 0
        && nowWall100ns > prev->wall100ns
        && cpuSum >= prev->cpuTime100ns;
    if (haveDelta) {
        const quint64 dCpu = cpuSum - prev->cpuTime100ns;
        const quint64 dWall = nowWall100ns - prev->wall100ns;
        const double pct = (100.0 * static_cast<double>(dCpu))
            / (static_cast<double>(dWall) * static_cast<double>(cpus));
        if (pct <= 0.0)
            result.cpuPercent = 0.0;
        else if (pct >= 100.0)
            result.cpuPercent = 100.0;
        else
            result.cpuPercent = pct;
    }

    if (outState) {
        outState->rootPid = rootPid;
        outState->cpuTime100ns = cpuSum;
        outState->wall100ns = nowWall100ns;
    }
    return result;
}

QString TerminalProcessStats::cpuLabel(bool valid, double cpuPercent)
{
    if (!valid)
        return QCoreApplication::translate("TerminalProcessStats", "CPU —");
    const int pct = qBound(0, qRound(cpuPercent), 100);
    return QCoreApplication::translate("TerminalProcessStats", "CPU %1%").arg(pct);
}

QString TerminalProcessStats::coreLoadLabel(const double *percent, int count)
{
    if (!percent || count <= 0)
        return {};

    QString out;
    out.reserve(static_cast<qsizetype>(count) * 16);
    for (int i = 0; i < count; ++i) {
        if (i > 0)
            out.append(QLatin1Char('\n'));
        const int rounded = qBound(0, qRound(percent[i]), 100);
        out.append(QCoreApplication::translate("TerminalProcessStats", "core%1: %2%")
                       .arg(i + 1)
                       .arg(rounded));
    }
    return out;
}

bool TerminalProcessStats::coreTipNeedsPush(bool tipVisible, bool overCpuLabel,
                                            const QString &shown, const QString &next)
{
    if (!overCpuLabel)
        return false;
    if (!tipVisible)
        return !next.isEmpty();
    return shown != next;
}

int TerminalProcessStats::statsPollMs(bool cpuTooltipOpen)
{
    return cpuTooltipOpen ? 200 : 1000;
}

void TerminalProcessStats::processorLoad(const ProcessorTick *now, int count,
                                         const ProcessorTick *prev,
                                         double *outPercent)
{
    if (!outPercent || count <= 0)
        return;
    for (int i = 0; i < count; ++i)
        outPercent[i] = 0.0;
    if (!now || !prev)
        return;
    for (int i = 0; i < count; ++i) {
        if (now[i].total100ns < prev[i].total100ns || now[i].idle100ns < prev[i].idle100ns)
            continue;
        const quint64 dTotal = now[i].total100ns - prev[i].total100ns;
        const quint64 dIdle = now[i].idle100ns - prev[i].idle100ns;
        if (dTotal == 0 || dIdle >= dTotal)
            continue;
        const double pct = (100.0 * static_cast<double>(dTotal - dIdle))
                           / static_cast<double>(dTotal);
        if (pct >= 100.0)
            outPercent[i] = 100.0;
        else if (pct > 0.0)
            outPercent[i] = pct;
    }
}

QString TerminalProcessStats::ramLabel(bool valid, quint64 rssBytes)
{
    if (!valid)
        return QCoreApplication::translate("TerminalProcessStats", "RAM —");
    constexpr quint64 kMiB = 1024ull * 1024ull;
    constexpr quint64 kGiB = 1024ull * kMiB;
    if (rssBytes >= kGiB) {
        const double gb = static_cast<double>(rssBytes) / static_cast<double>(kGiB);
        return QCoreApplication::translate("TerminalProcessStats", "RAM %1 GB")
            .arg(gb, 0, 'f', 1);
    }
    return QCoreApplication::translate("TerminalProcessStats", "RAM %1 MB")
        .arg(rssBytes / kMiB);
}

quint64 TerminalProcessStats::nowWall100ns()
{
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count();
    if (ns <= 0)
        return 0;
    return static_cast<quint64>(ns) / 100ull;
}

int TerminalProcessStats::logicalCpuCount()
{
    const int n = QThread::idealThreadCount();
    return n > 0 ? n : 1;
}

#ifdef Q_OS_WIN

namespace {

struct UnicodeString {
    quint16 length;
    quint16 maximumLength;
    wchar_t *buffer;
};

struct SystemProcessInformation {
    ULONG nextEntryOffset;
    ULONG numberOfThreads;
    LARGE_INTEGER workingSetPrivateSize;
    ULONG hardFaultCount;
    ULONG numberOfThreadsHighWatermark;
    ULONGLONG cycleTime;
    LARGE_INTEGER createTime;
    LARGE_INTEGER userTime;
    LARGE_INTEGER kernelTime;
    UnicodeString imageName;
    LONG basePriority;
    HANDLE uniqueProcessId;
    HANDLE inheritedFromUniqueProcessId;
    ULONG handleCount;
    ULONG sessionId;
    ULONG_PTR uniqueProcessKey;
    SIZE_T peakVirtualSize;
    SIZE_T virtualSize;
    ULONG pageFaultCount;
    SIZE_T peakWorkingSetSize;
    SIZE_T workingSetSize;
};

static_assert(offsetof(SystemProcessInformation, uniqueProcessId) == 80,
              "SYSTEM_PROCESS_INFORMATION UniqueProcessId offset (x64)");
static_assert(offsetof(SystemProcessInformation, workingSetSize) == 144,
              "SYSTEM_PROCESS_INFORMATION WorkingSetSize offset (x64)");

using NtQuerySystemInformationFn = LONG(WINAPI *)(ULONG, PVOID, ULONG, PULONG);

QByteArray &ntProcessBuffer()
{
    static QByteArray buf;
    return buf;
}

} // namespace

bool TerminalProcessStats::enumerateProcesses(QVector<ProcessSample> &out)
{
    out.clear();

    static NtQuerySystemInformationFn ntQuery = nullptr;
    if (!ntQuery) {
        ntQuery = reinterpret_cast<NtQuerySystemInformationFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    }
    if (!ntQuery)
        return false;

    QByteArray &buf = ntProcessBuffer();
    if (buf.size() < 256 * 1024)
        buf.resize(256 * 1024);

    constexpr ULONG kSystemProcessInformation = 5;
    constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004);
    for (;;) {
        ULONG got = 0;
        const LONG st = ntQuery(kSystemProcessInformation, buf.data(),
                                static_cast<ULONG>(buf.size()), &got);
        if (st >= 0)
            break;
        if (st != kStatusInfoLengthMismatch)
            return false;
        int next = buf.size() * 2;
        if (got != 0 && static_cast<int>(got) + 64 * 1024 > next)
            next = static_cast<int>(got) + 64 * 1024;
        if (next < buf.size() + 64 * 1024)
            next = buf.size() + 64 * 1024;
        buf.resize(next);
    }

    const char *p = buf.constData();
    const char *const end = p + buf.size();
    out.reserve(256);
    for (;;) {
        if (p + sizeof(SystemProcessInformation) > end)
            break;
        const auto *sp = reinterpret_cast<const SystemProcessInformation *>(p);
        ProcessSample s;
        s.pid = static_cast<quint32>(reinterpret_cast<quintptr>(sp->uniqueProcessId));
        s.parentPid = static_cast<quint32>(reinterpret_cast<quintptr>(sp->inheritedFromUniqueProcessId));
        const quint64 user = static_cast<quint64>(sp->userTime.QuadPart);
        const quint64 kernel = static_cast<quint64>(sp->kernelTime.QuadPart);
        s.cpuTime100ns = user + kernel;
        s.rssBytes = static_cast<quint64>(sp->workingSetSize);
        if (s.pid != 0)
            out.append(s);
        if (sp->nextEntryOffset == 0)
            break;
        p += sp->nextEntryOffset;
    }
    return true;
}

#elif defined(Q_OS_LINUX)

bool TerminalProcessStats::enumerateProcesses(QVector<ProcessSample> &out)
{
    out.clear();
    DIR *dir = opendir("/proc");
    if (!dir)
        return false;
    const long clk = sysconf(_SC_CLK_TCK);
    const long page = sysconf(_SC_PAGESIZE);
    if (clk <= 0 || page <= 0) {
        closedir(dir);
        return false;
    }

    out.reserve(256);
    while (dirent *ent = readdir(dir)) {
        char *end = nullptr;
        const unsigned long pid = strtoul(ent->d_name, &end, 10);
        if (!end || *end != '\0' || pid == 0)
            continue;

        char path[64];
        snprintf(path, sizeof(path), "/proc/%lu/stat", pid);
        FILE *f = fopen(path, "r");
        if (!f)
            continue;
        char buf[4096];
        const size_t n = fread(buf, 1, sizeof(buf) - 1, f);
        fclose(f);
        if (n == 0)
            continue;
        buf[n] = '\0';

        char *rparen = strrchr(buf, ')');
        if (!rparen)
            continue;

        int field = 2;
        const char *s = rparen + 1;
        quint32 ppid = 0;
        unsigned long utime = 0;
        unsigned long stime = 0;
        unsigned long rssPages = 0;
        while (*s) {
            while (*s == ' ')
                ++s;
            if (!*s)
                break;
            ++field;
            if (field == 3) {
                if (*s)
                    ++s;
                continue;
            }
            char *next = nullptr;
            const unsigned long v = strtoul(s, &next, 10);
            if (next == s)
                break;
            s = next;
            if (field == 4)
                ppid = static_cast<quint32>(v);
            else if (field == 14)
                utime = v;
            else if (field == 15)
                stime = v;
            else if (field == 24) {
                rssPages = v;
                break;
            }
        }

        ProcessSample sample;
        sample.pid = static_cast<quint32>(pid);
        sample.parentPid = ppid;
        sample.cpuTime100ns = ((utime + stime) * 10000000ull) / static_cast<quint64>(clk);
        sample.rssBytes = rssPages * static_cast<quint64>(page);
        out.append(sample);
    }
    closedir(dir);
    return true;
}

#elif defined(Q_OS_MACOS)

bool TerminalProcessStats::enumerateProcesses(QVector<ProcessSample> &out)
{
    out.clear();
    const int bufBytes = proc_listpids(PROC_ALL_PIDS, 0, nullptr, 0);
    if (bufBytes <= 0)
        return false;
    QVector<pid_t> pids(bufBytes / static_cast<int>(sizeof(pid_t)));
    const int gotBytes = proc_listpids(PROC_ALL_PIDS, 0, pids.data(),
                                       static_cast<int>(pids.size() * sizeof(pid_t)));
    if (gotBytes <= 0)
        return false;
    const int n = gotBytes / static_cast<int>(sizeof(pid_t));
    out.reserve(n);
    for (int i = 0; i < n; ++i) {
        const pid_t pid = pids.at(i);
        if (pid <= 0)
            continue;
        proc_taskallinfo info{};
        const int sz = proc_pidinfo(pid, PROC_PIDTASKALLINFO, 0, &info, sizeof(info));
        if (sz != static_cast<int>(sizeof(info)))
            continue;
        ProcessSample s;
        s.pid = static_cast<quint32>(pid);
        s.parentPid = static_cast<quint32>(info.pbsd.pbi_ppid);
        s.cpuTime100ns = (static_cast<quint64>(info.ptinfo.pti_total_user)
                          + static_cast<quint64>(info.ptinfo.pti_total_system))
                         / 100ull;
        s.rssBytes = static_cast<quint64>(info.ptinfo.pti_resident_size);
        out.append(s);
    }
    return true;
}

#else

bool TerminalProcessStats::enumerateProcesses(QVector<ProcessSample> &out)
{
    out.clear();
    return false;
}

#endif

bool TerminalProcessStats::enumerateProcessorTicks(QVector<ProcessorTick> &out)
{
    out.clear();
#ifdef Q_OS_WIN
    struct Spi {
        LARGE_INTEGER idle;
        LARGE_INTEGER kernel;
        LARGE_INTEGER user;
        LARGE_INTEGER dpc;
        LARGE_INTEGER interruptTime;
        ULONG interruptCount;
    };
    static_assert(sizeof(Spi) == 48, "SYSTEM_PROCESSOR_PERFORMANCE_INFORMATION x64 size");

    using NtQuerySystemInformationFn = LONG(WINAPI *)(ULONG, PVOID, ULONG, PULONG);
    static NtQuerySystemInformationFn ntQuery = nullptr;
    if (!ntQuery) {
        ntQuery = reinterpret_cast<NtQuerySystemInformationFn>(
            GetProcAddress(GetModuleHandleW(L"ntdll.dll"), "NtQuerySystemInformation"));
    }
    if (!ntQuery)
        return false;

    static QByteArray buf;
    constexpr int kRecord = 48;
    if (buf.size() < kRecord)
        buf.resize(kRecord * 16);

    constexpr ULONG kSystemProcessorPerformanceInformation = 8;
    constexpr LONG kStatusInfoLengthMismatch = static_cast<LONG>(0xC0000004);
    for (int attempt = 0; attempt < 4; ++attempt) {
        ULONG got = 0;
        const LONG st = ntQuery(kSystemProcessorPerformanceInformation, buf.data(),
                                static_cast<ULONG>(buf.size()), &got);
        if (st >= 0) {
            if (got < static_cast<ULONG>(kRecord) || (got % static_cast<ULONG>(kRecord)) != 0)
                return false;
            const int n = static_cast<int>(got / static_cast<ULONG>(kRecord));
            out.resize(n);
            const char *p = buf.constData();
            for (int i = 0; i < n; ++i) {
                Spi sp;
                std::memcpy(&sp, p + static_cast<ptrdiff_t>(i) * kRecord, sizeof(sp));
                const quint64 idle = static_cast<quint64>(sp.idle.QuadPart);
                const quint64 kernel = static_cast<quint64>(sp.kernel.QuadPart);
                const quint64 user = static_cast<quint64>(sp.user.QuadPart);
                // KernelTime includes IdleTime. Core capacity is kernel + user.
                out[i].idle100ns = idle;
                out[i].total100ns = kernel + user;
            }
            return true;
        }
        if (st != kStatusInfoLengthMismatch || got == 0)
            return false;
        int next = static_cast<int>(got);
        if (next % kRecord)
            next += kRecord - (next % kRecord);
        if (next <= buf.size())
            next = buf.size() + kRecord;
        buf.resize(next);
    }
    return false;
#elif defined(Q_OS_LINUX)
    FILE *f = fopen("/proc/stat", "r");
    if (!f)
        return false;
    const long clk = sysconf(_SC_CLK_TCK);
    if (clk <= 0) {
        fclose(f);
        return false;
    }
    char line[512];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] != 'c' || line[1] != 'p' || line[2] != 'u')
            continue;
        if (line[3] < '0' || line[3] > '9')
            continue;
        const char *s = line;
        while (*s && *s != ' ')
            ++s;
        unsigned long user = 0, nice = 0, system = 0, idle = 0;
        unsigned long iowait = 0, irq = 0, softirq = 0, steal = 0;
        if (sscanf(s, "%lu %lu %lu %lu %lu %lu %lu %lu",
                   &user, &nice, &system, &idle, &iowait, &irq, &softirq, &steal) < 4)
            continue;
        const quint64 idleTicks = static_cast<quint64>(idle) + iowait;
        const quint64 totalTicks = static_cast<quint64>(user) + nice + system + idle
                                   + iowait + irq + softirq + steal;
        ProcessorTick tick;
        tick.idle100ns = (idleTicks * 10000000ull) / static_cast<quint64>(clk);
        tick.total100ns = (totalTicks * 10000000ull) / static_cast<quint64>(clk);
        out.append(tick);
    }
    fclose(f);
    return !out.isEmpty();
#elif defined(Q_OS_MACOS)
    processor_info_array_t cpuInfo = nullptr;
    mach_msg_type_number_t numCpuInfo = 0;
    natural_t ncpu = 0;
    const kern_return_t kr = host_processor_info(mach_host_self(), PROCESSOR_CPU_LOAD_INFO,
                                                 &ncpu, &cpuInfo, &numCpuInfo);
    if (kr != KERN_SUCCESS || ncpu == 0 || !cpuInfo)
        return false;
    const auto *load = reinterpret_cast<processor_cpu_load_info_t>(cpuInfo);
    out.resize(static_cast<int>(ncpu));
    for (natural_t i = 0; i < ncpu; ++i) {
        const auto &ticks = load[i].cpu_ticks;
        const quint64 idle = ticks[CPU_STATE_IDLE];
        out[static_cast<int>(i)].idle100ns = idle;
        out[static_cast<int>(i)].total100ns = ticks[CPU_STATE_USER] + ticks[CPU_STATE_SYSTEM]
                                              + idle + ticks[CPU_STATE_NICE];
    }
    vm_deallocate(mach_task_self(), reinterpret_cast<vm_address_t>(cpuInfo),
                  static_cast<vm_size_t>(numCpuInfo) * sizeof(integer_t));
    return true;
#else
    return false;
#endif
}
