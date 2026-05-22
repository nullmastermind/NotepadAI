/*
 * NotepadADE addition: crash reporting.
 *
 * Design (locked at plan v4):
 *  - Primary path: cwd/crash_report.txt; fallback: %APPDATA%/NotepadAI/crashes
 *    on Windows or $XDG_DATA_HOME/NotepadAI/crashes on POSIX. Both resolved at
 *    install() time via raw Win32 / POSIX (no Qt dependency, no allocation
 *    inside handlers).
 *  - One file, append, per-section atomic writes. Each section is built in a
 *    pre-allocated 4KB buffer (fits under PIPE_BUF on POSIX → atomic with
 *    O_APPEND) and flushed with FlushFileBuffers / fsync before the next.
 *    Record boundaries are "===== CRASH BEGIN <uuid> =====" / "===== CRASH END =====".
 *  - UUID = "<pid>-<tid>-<monotonic_ns>" — no entropy source, only async-safe
 *    primitives (getpid/GetCurrentProcessId, syscall(SYS_gettid)/GetCurrentThreadId,
 *    clock_gettime(CLOCK_MONOTONIC)/QueryPerformanceCounter).
 *  - Windows: AddVectoredExceptionHandler (front of chain, can't be overridden
 *    by third-party SetUnhandledExceptionFilter) + SetUnhandledExceptionFilter
 *    as backup. Vectored filter only fires for known-fatal codes.
 *  - POSIX: sigaction with SA_ONSTACK|SA_SIGINFO|SA_RESETHAND on a 64KB
 *    pre-allocated alternate signal stack (so SIGSEGV from stack overflow has
 *    room to run).
 *  - std::set_terminate for unhandled C++ exceptions.
 *  - No Qt API called inside any handler. Reads only static char buffers
 *    populated by CrashContext from the UI thread.
 *  - Pre-flight rotation: at install() time, if crash_report.txt > 10MB, rename
 *    to .1 (overwriting any previous .1). Keeps one generation, no runtime
 *    rotation in handler.
 */

#include "CrashHandler.h"
#include "CrashContext.h"

#include <atomic>
#include <csignal>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <new>
#include <stdexcept>

#ifdef _WIN32
#  define WIN32_LEAN_AND_MEAN
#  include <windows.h>
#  include <dbghelp.h>
#  include <shlobj.h>
#  pragma comment(lib, "dbghelp.lib")
#  pragma comment(lib, "shell32.lib")
#else
#  include <fcntl.h>
#  include <pthread.h>
#  include <sys/stat.h>
#  include <sys/syscall.h>
#  include <sys/types.h>
#  include <unistd.h>
#  if __has_include(<execinfo.h>)
#    include <execinfo.h>
#    define CRASH_HANDLER_HAVE_BACKTRACE 1
#  endif
#endif

namespace {

// ---- Constants ---------------------------------------------------------
constexpr std::size_t kPathMax        = 4096;
constexpr std::size_t kSectionBufSize = 4096;       // ≤ PIPE_BUF for atomic O_APPEND
constexpr int         kMaxStackFrames = 128;
constexpr std::size_t kRotateBytes    = 10 * 1024 * 1024;
// Reserve enough headroom for the worst-case frame line (path + symbol + line
// number can easily reach ~700B). Flush *before* writing a frame if remaining
// space is below this — otherwise appendStr silently truncates and frames are
// dropped.
constexpr std::size_t kFrameReserve   = 1024;
// If SymFromAddr's reported offset from the matched symbol exceeds this, the
// "symbol" is almost certainly the nearest export of a PDB-less module, not
// the real function. Suppress the misleading name in that case.
constexpr DWORD64     kSymDispMax     = 256 * 1024;

#ifndef _WIN32
constexpr std::size_t kAltStackSize = 1 << 16;      // 64KB
#endif

// ---- Static state (zero-initialised, no allocation in handler) ---------
char g_primary_path[kPathMax]  = {0};
char g_fallback_path[kPathMax] = {0};
char g_section_buf[kSectionBufSize];
std::atomic<bool> g_reporting{false};

#ifndef _WIN32
alignas(16) char g_alt_stack[kAltStackSize];
#endif

// ---- File abstraction (raw OS handles, no CRT) --------------------------
struct CrashFile {
#ifdef _WIN32
    HANDLE h = INVALID_HANDLE_VALUE;
    bool open(const char *path) {
        h = CreateFileA(path,
                        FILE_APPEND_DATA, FILE_SHARE_READ,
                        nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
        return h != INVALID_HANDLE_VALUE;
    }
    bool write(const void *data, std::size_t len) {
        DWORD written = 0;
        return WriteFile(h, data, static_cast<DWORD>(len), &written, nullptr)
            && written == len;
    }
    void flush() { FlushFileBuffers(h); }
    void close() {
        if (h != INVALID_HANDLE_VALUE) { CloseHandle(h); h = INVALID_HANDLE_VALUE; }
    }
#else
    int fd = -1;
    bool open(const char *path) {
        fd = ::open(path, O_WRONLY | O_CREAT | O_APPEND, 0644);
        return fd >= 0;
    }
    bool write(const void *data, std::size_t len) {
        const ssize_t w = ::write(fd, data, len);
        return w >= 0 && static_cast<std::size_t>(w) == len;
    }
    void flush() { ::fsync(fd); }
    void close() {
        if (fd >= 0) { ::close(fd); fd = -1; }
    }
#endif
};

// ---- Async-safe formatters (no printf, no allocator) -------------------
bool appendStr(char *buf, std::size_t cap, std::size_t *pos, const char *s)
{
    if (!s) return true;
    while (*s && *pos + 1 < cap) buf[(*pos)++] = *s++;
    buf[*pos] = '\0';
    return *s == '\0';
}

bool appendChar(char *buf, std::size_t cap, std::size_t *pos, char c)
{
    if (*pos + 1 < cap) { buf[(*pos)++] = c; buf[*pos] = '\0'; return true; }
    return false;
}

bool appendU64(char *buf, std::size_t cap, std::size_t *pos, std::uint64_t v)
{
    char tmp[21];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) { tmp[n++] = static_cast<char>('0' + (v % 10)); v /= 10; }
    while (n--) if (!appendChar(buf, cap, pos, tmp[n])) return false;
    return true;
}

bool appendHex64(char *buf, std::size_t cap, std::size_t *pos, std::uint64_t v, int width)
{
    char tmp[17];
    int n = 0;
    if (v == 0) tmp[n++] = '0';
    while (v) {
        unsigned d = v & 0xF;
        tmp[n++] = static_cast<char>(d < 10 ? '0' + d : 'a' + (d - 10));
        v >>= 4;
    }
    while (n < width) tmp[n++] = '0';
    while (n--) if (!appendChar(buf, cap, pos, tmp[n])) return false;
    return true;
}

void appendTimestampUtc(char *buf, std::size_t cap, std::size_t *pos)
{
    const std::time_t now = std::time(nullptr);
    std::tm tm{};
#ifdef _WIN32
    gmtime_s(&tm, &now);
#else
    gmtime_r(&now, &tm);
#endif
    auto pad2 = [&](int v) {
        appendChar(buf, cap, pos, static_cast<char>('0' + (v / 10) % 10));
        appendChar(buf, cap, pos, static_cast<char>('0' + v % 10));
    };
    appendU64(buf, cap, pos, static_cast<std::uint64_t>(tm.tm_year) + 1900);
    appendChar(buf, cap, pos, '-');
    pad2(tm.tm_mon + 1);
    appendChar(buf, cap, pos, '-');
    pad2(tm.tm_mday);
    appendChar(buf, cap, pos, ' ');
    pad2(tm.tm_hour);
    appendChar(buf, cap, pos, ':');
    pad2(tm.tm_min);
    appendChar(buf, cap, pos, ':');
    pad2(tm.tm_sec);
    appendStr(buf, cap, pos, " UTC");
}

// UUID = "<pid>-<tid>-<monotonic_ns>"
void appendUuid(char *buf, std::size_t cap, std::size_t *pos)
{
#ifdef _WIN32
    const std::uint64_t pid = GetCurrentProcessId();
    const std::uint64_t tid = GetCurrentThreadId();
    LARGE_INTEGER freq{}, ctr{};
    QueryPerformanceFrequency(&freq);
    QueryPerformanceCounter(&ctr);
    const std::uint64_t ns = freq.QuadPart
        ? static_cast<std::uint64_t>(ctr.QuadPart * 1000000000ULL
                                    / static_cast<std::uint64_t>(freq.QuadPart))
        : 0;
#else
    const std::uint64_t pid = static_cast<std::uint64_t>(getpid());
#  if defined(__linux__) && defined(SYS_gettid)
    const std::uint64_t tid = static_cast<std::uint64_t>(syscall(SYS_gettid));
#  else
    const std::uint64_t tid = reinterpret_cast<std::uint64_t>(pthread_self());
#  endif
    timespec ts{};
    clock_gettime(CLOCK_MONOTONIC, &ts);
    const std::uint64_t ns = static_cast<std::uint64_t>(ts.tv_sec) * 1000000000ULL
                           + static_cast<std::uint64_t>(ts.tv_nsec);
#endif
    appendU64(buf, cap, pos, pid);
    appendChar(buf, cap, pos, '-');
    appendU64(buf, cap, pos, tid);
    appendChar(buf, cap, pos, '-');
    appendU64(buf, cap, pos, ns);
}

// ---- Path resolution (install-time only, may allocate / call OS APIs) --
void captureCwdPath()
{
    char cwd[kPathMax - 32] = {0};
#ifdef _WIN32
    const DWORD n = GetCurrentDirectoryA(static_cast<DWORD>(sizeof(cwd)), cwd);
    if (n == 0 || n >= sizeof(cwd)) { g_primary_path[0] = '\0'; return; }
    std::size_t p = 0;
    appendStr(g_primary_path, kPathMax, &p, cwd);
    appendStr(g_primary_path, kPathMax, &p, "\\crash_report.txt");
#else
    if (getcwd(cwd, sizeof(cwd)) == nullptr) { g_primary_path[0] = '\0'; return; }
    std::size_t p = 0;
    appendStr(g_primary_path, kPathMax, &p, cwd);
    appendStr(g_primary_path, kPathMax, &p, "/crash_report.txt");
#endif
}

#ifdef _WIN32
void ensureDirRecursive(const char *dir)
{
    char tmp[kPathMax];
    std::size_t p = 0;
    appendStr(tmp, sizeof(tmp), &p, dir);
    for (std::size_t i = 3; i <= p; ++i) {
        if (tmp[i] == '\\' || tmp[i] == '/' || i == p) {
            const char saved = tmp[i];
            tmp[i] = '\0';
            CreateDirectoryA(tmp, nullptr);
            tmp[i] = saved;
        }
    }
}
#else
void ensureDirRecursive(const char *dir)
{
    char tmp[kPathMax];
    std::size_t p = 0;
    appendStr(tmp, sizeof(tmp), &p, dir);
    for (std::size_t i = 1; i <= p; ++i) {
        if (tmp[i] == '/' || i == p) {
            const char saved = tmp[i];
            tmp[i] = '\0';
            ::mkdir(tmp, 0755);
            tmp[i] = saved;
        }
    }
}
#endif

void captureFallbackPath()
{
#ifdef _WIN32
    char appdata[MAX_PATH] = {0};
    if (!SUCCEEDED(SHGetFolderPathA(nullptr, CSIDL_APPDATA, nullptr, 0, appdata))) {
        g_fallback_path[0] = '\0';
        return;
    }
    char dir[kPathMax];
    std::size_t dp = 0;
    appendStr(dir, sizeof(dir), &dp, appdata);
    appendStr(dir, sizeof(dir), &dp, "\\NotepadAI\\crashes");
    ensureDirRecursive(dir);

    std::size_t p = 0;
    appendStr(g_fallback_path, kPathMax, &p, dir);
    appendStr(g_fallback_path, kPathMax, &p, "\\crash_report.txt");
#else
    const char *xdg  = std::getenv("XDG_DATA_HOME");
    const char *home = std::getenv("HOME");
    char dir[kPathMax];
    std::size_t dp = 0;
    if (xdg && xdg[0]) {
        appendStr(dir, sizeof(dir), &dp, xdg);
        appendStr(dir, sizeof(dir), &dp, "/NotepadAI/crashes");
    } else if (home) {
        appendStr(dir, sizeof(dir), &dp, home);
        appendStr(dir, sizeof(dir), &dp, "/.local/share/NotepadAI/crashes");
    } else {
        g_fallback_path[0] = '\0';
        return;
    }
    ensureDirRecursive(dir);

    std::size_t p = 0;
    appendStr(g_fallback_path, kPathMax, &p, dir);
    appendStr(g_fallback_path, kPathMax, &p, "/crash_report.txt");
#endif
}

void rotateIfLarge(const char *path)
{
    if (!path[0]) return;
#ifdef _WIN32
    WIN32_FILE_ATTRIBUTE_DATA fa{};
    if (!GetFileAttributesExA(path, GetFileExInfoStandard, &fa)) return;
    ULARGE_INTEGER sz{};
    sz.LowPart  = fa.nFileSizeLow;
    sz.HighPart = fa.nFileSizeHigh;
    if (sz.QuadPart < kRotateBytes) return;
    char rot[kPathMax];
    std::size_t p = 0;
    appendStr(rot, sizeof(rot), &p, path);
    appendStr(rot, sizeof(rot), &p, ".1");
    DeleteFileA(rot);
    MoveFileA(path, rot);
#else
    struct stat st{};
    if (::stat(path, &st) != 0) return;
    if (st.st_size < static_cast<off_t>(kRotateBytes)) return;
    char rot[kPathMax];
    std::size_t p = 0;
    appendStr(rot, sizeof(rot), &p, path);
    appendStr(rot, sizeof(rot), &p, ".1");
    ::unlink(rot);
    ::rename(path, rot);
#endif
}

// ---- Report writer (handler-context, async-signal safe on POSIX path) --

bool openReport(CrashFile *cf)
{
    if (g_primary_path[0] && cf->open(g_primary_path)) return true;
    if (g_fallback_path[0] && cf->open(g_fallback_path)) return true;
    return false;
}

void writeSection(CrashFile *cf, char *buf, std::size_t *pos)
{
    if (*pos == 0) return;
    cf->write(buf, *pos);
    cf->flush();
    *pos = 0;
    buf[0] = '\0';
}

void writeHeader(CrashFile *cf, const char *kind)
{
    std::size_t p = 0;
    char *b = g_section_buf;
    appendStr(b, kSectionBufSize, &p, "\n===== CRASH BEGIN ");
    appendUuid(b, kSectionBufSize, &p);
    appendStr(b, kSectionBufSize, &p, " =====\n");
    appendStr(b, kSectionBufSize, &p, "Time : ");
    appendTimestampUtc(b, kSectionBufSize, &p);
    appendStr(b, kSectionBufSize, &p, "\nType : ");
    appendStr(b, kSectionBufSize, &p, kind);
#ifdef APP_VERSION
    appendStr(b, kSectionBufSize, &p, "\nVer  : ");
    appendStr(b, kSectionBufSize, &p, APP_VERSION);
#endif
    appendStr(b, kSectionBufSize, &p, "\nPID  : ");
#ifdef _WIN32
    appendU64(b, kSectionBufSize, &p, GetCurrentProcessId());
#else
    appendU64(b, kSectionBufSize, &p, static_cast<std::uint64_t>(getpid()));
#endif
    appendChar(b, kSectionBufSize, &p, '\n');
    writeSection(cf, b, &p);
}

void writeContextSection(CrashFile *cf)
{
    std::size_t p = 0;
    char *b = g_section_buf;
    appendStr(b, kSectionBufSize, &p, "Context:\n  Last action     : ");
    appendStr(b, kSectionBufSize, &p,
              CrashContext::lastAction[0] ? CrashContext::lastAction : "(none)");
    appendStr(b, kSectionBufSize, &p, "\n  Last action ts  : ");
    appendU64(b, kSectionBufSize, &p,
              CrashContext::lastActionTimestampMs.load(std::memory_order_relaxed));
    appendStr(b, kSectionBufSize, &p, " (ms since epoch)\n  Active editor   : ");
    appendStr(b, kSectionBufSize, &p,
              CrashContext::activeEditorPath[0] ? CrashContext::activeEditorPath : "(none)");
    appendStr(b, kSectionBufSize, &p, "\n  Active dock     : ");
    appendStr(b, kSectionBufSize, &p,
              CrashContext::activeDockId[0] ? CrashContext::activeDockId : "(none)");
    appendStr(b, kSectionBufSize, &p, "\n  Active workspace: ");
    appendStr(b, kSectionBufSize, &p,
              CrashContext::activeWorkspaceRoot[0] ? CrashContext::activeWorkspaceRoot : "(none)");
    appendChar(b, kSectionBufSize, &p, '\n');
    writeSection(cf, b, &p);
}

void writeFooter(CrashFile *cf)
{
    std::size_t p = 0;
    char *b = g_section_buf;
    appendStr(b, kSectionBufSize, &p, "===== CRASH END =====\n");
    writeSection(cf, b, &p);
}

void emitStderrHint()
{
    static const char msg[] = "[CRASH] see crash_report.txt\n";
#ifdef _WIN32
    HANDLE h = GetStdHandle(STD_ERROR_HANDLE);
    if (h && h != INVALID_HANDLE_VALUE) {
        DWORD w = 0;
        WriteFile(h, msg, static_cast<DWORD>(sizeof(msg) - 1), &w, nullptr);
    }
#else
    const ssize_t r = ::write(STDERR_FILENO, msg, sizeof(msg) - 1);
    (void)r;
#endif
}

// ---- Stack capture ------------------------------------------------------

#ifdef _WIN32

const char *sehCodeName(DWORD code)
{
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:    return "ARRAY_BOUNDS_EXCEEDED";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_FLT_DIVIDE_BY_ZERO:       return "FLT_DIVIDE_BY_ZERO";
        case EXCEPTION_FLT_INVALID_OPERATION:    return "FLT_INVALID_OPERATION";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_INT_OVERFLOW:             return "INT_OVERFLOW";
        case EXCEPTION_NONCONTINUABLE_EXCEPTION: return "NONCONTINUABLE_EXCEPTION";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        default:                                 return "UNKNOWN";
    }
}

bool isFatalSehCode(DWORD code)
{
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:
        case EXCEPTION_STACK_OVERFLOW:
        case EXCEPTION_ILLEGAL_INSTRUCTION:
        case EXCEPTION_INT_DIVIDE_BY_ZERO:
        case EXCEPTION_PRIV_INSTRUCTION:
        case EXCEPTION_IN_PAGE_ERROR:
        case EXCEPTION_NONCONTINUABLE_EXCEPTION:
        case EXCEPTION_ARRAY_BOUNDS_EXCEEDED:
        case EXCEPTION_DATATYPE_MISALIGNMENT:
            return true;
        default:
            return false;
    }
}

void writeStackTraceWindows(CrashFile *cf, CONTEXT *ctx)
{
    HANDLE process = GetCurrentProcess();
    HANDLE thread  = GetCurrentThread();

    STACKFRAME64 frame{};
    DWORD machine = 0;
#if defined(_M_X64) || defined(__x86_64__)
    machine = IMAGE_FILE_MACHINE_AMD64;
    frame.AddrPC.Offset    = ctx->Rip;
    frame.AddrFrame.Offset = ctx->Rbp;
    frame.AddrStack.Offset = ctx->Rsp;
#elif defined(_M_ARM64) || defined(__aarch64__)
    machine = IMAGE_FILE_MACHINE_ARM64;
    frame.AddrPC.Offset    = ctx->Pc;
    frame.AddrFrame.Offset = ctx->Fp;
    frame.AddrStack.Offset = ctx->Sp;
#else
    std::size_t p = 0;
    appendStr(g_section_buf, kSectionBufSize, &p, "Stack: (unsupported arch)\n");
    writeSection(cf, g_section_buf, &p);
    return;
#endif
    frame.AddrPC.Mode = frame.AddrFrame.Mode = frame.AddrStack.Mode = AddrModeFlat;

    {
        std::size_t p = 0;
        appendStr(g_section_buf, kSectionBufSize, &p, "Stack (most recent first):\n");
        writeSection(cf, g_section_buf, &p);
    }

    DWORD64 lastPc = 0;
    int dupCount = 0;
    std::size_t p = 0;

    auto flushCycle = [&]() {
        if (dupCount >= 3) {
            appendStr(g_section_buf, kSectionBufSize, &p, "  [");
            appendU64(g_section_buf, kSectionBufSize, &p,
                      static_cast<std::uint64_t>(dupCount));
            appendStr(g_section_buf, kSectionBufSize, &p,
                      " more identical frames omitted]\n");
        }
        dupCount = 0;
    };

    for (int i = 0; i < kMaxStackFrames; ++i) {
        if (!StackWalk64(machine, process, thread, &frame, ctx, nullptr,
                         SymFunctionTableAccess64, SymGetModuleBase64, nullptr)) {
            break;
        }
        if (frame.AddrPC.Offset == 0) break;

        if (frame.AddrPC.Offset == lastPc) {
            ++dupCount;
            if (dupCount >= 3) continue;  // collapse runaway recursion
        } else {
            flushCycle();
        }
        lastPc = frame.AddrPC.Offset;

        // Flush *before* the next frame if there isn't enough room for its
        // worst-case formatting. Counting frames written (the previous design)
        // silently truncated when paths + symbol names overflowed the buffer.
        if (kSectionBufSize - p < kFrameReserve) {
            writeSection(cf, g_section_buf, &p);
        }

        alignas(SYMBOL_INFO) char symBuf[sizeof(SYMBOL_INFO) + 256];
        SYMBOL_INFO *sym = reinterpret_cast<SYMBOL_INFO *>(symBuf);
        sym->SizeOfStruct = sizeof(SYMBOL_INFO);
        sym->MaxNameLen   = 255;
        DWORD64 symDisp = 0;
        const bool symLookupOk = SymFromAddr(process, frame.AddrPC.Offset, &symDisp, sym) != 0;
        // Reject implausibly-distant matches: SymFromAddr on a PDB-less module
        // (e.g. shipped Qt) returns the nearest exported symbol, which can be
        // hundreds of KB away and is more misleading than helpful.
        const bool haveSym = symLookupOk && symDisp <= kSymDispMax;

        IMAGEHLP_LINE64 line{};
        line.SizeOfStruct = sizeof(IMAGEHLP_LINE64);
        DWORD lineDisp = 0;
        const bool haveLine =
            SymGetLineFromAddr64(process, frame.AddrPC.Offset, &lineDisp, &line) != 0;

        IMAGEHLP_MODULE64 mod{};
        mod.SizeOfStruct = sizeof(IMAGEHLP_MODULE64);
        const bool haveMod = SymGetModuleInfo64(process, frame.AddrPC.Offset, &mod) != 0;

        appendStr(g_section_buf, kSectionBufSize, &p, "  #");
        if (i < 100) appendChar(g_section_buf, kSectionBufSize, &p,
                                static_cast<char>('0' + (i / 10)));
        appendChar(g_section_buf, kSectionBufSize, &p, static_cast<char>('0' + (i % 10)));
        appendStr(g_section_buf, kSectionBufSize, &p, " 0x");
        appendHex64(g_section_buf, kSectionBufSize, &p, frame.AddrPC.Offset, 16);
        appendChar(g_section_buf, kSectionBufSize, &p, ' ');
        appendStr(g_section_buf, kSectionBufSize, &p, haveMod ? mod.ModuleName : "?");
        if (haveSym) {
            appendChar(g_section_buf, kSectionBufSize, &p, '!');
            appendStr(g_section_buf, kSectionBufSize, &p, sym->Name);
            appendStr(g_section_buf, kSectionBufSize, &p, "+0x");
            appendHex64(g_section_buf, kSectionBufSize, &p, symDisp, 0);
        } else if (haveMod && mod.BaseOfImage != 0
                   && frame.AddrPC.Offset >= mod.BaseOfImage) {
            // No usable symbol — print module-relative offset so the address
            // can still be resolved post-mortem with a matching PDB.
            appendStr(g_section_buf, kSectionBufSize, &p, "+0x");
            appendHex64(g_section_buf, kSectionBufSize, &p,
                        frame.AddrPC.Offset - mod.BaseOfImage, 0);
        } else {
            appendStr(g_section_buf, kSectionBufSize, &p, "!(no symbol)");
        }
        if (haveLine) {
            appendStr(g_section_buf, kSectionBufSize, &p, "  [");
            appendStr(g_section_buf, kSectionBufSize, &p, line.FileName);
            appendChar(g_section_buf, kSectionBufSize, &p, ':');
            appendU64(g_section_buf, kSectionBufSize, &p, line.LineNumber);
            appendChar(g_section_buf, kSectionBufSize, &p, ']');
        }
        appendChar(g_section_buf, kSectionBufSize, &p, '\n');
    }
    flushCycle();
    writeSection(cf, g_section_buf, &p);
}

bool writeReportSeh(EXCEPTION_POINTERS *info, const char *origin)
{
    CrashFile cf;
    if (!openReport(&cf)) return false;

    writeHeader(&cf, origin);

    {
        std::size_t p = 0;
        char *b = g_section_buf;
        const DWORD code = info->ExceptionRecord->ExceptionCode;
        appendStr(b, kSectionBufSize, &p, "Code : 0x");
        appendHex64(b, kSectionBufSize, &p, code, 8);
        appendStr(b, kSectionBufSize, &p, " (");
        appendStr(b, kSectionBufSize, &p, sehCodeName(code));
        appendStr(b, kSectionBufSize, &p, ")\nAddr : 0x");
        appendHex64(b, kSectionBufSize, &p,
                    reinterpret_cast<std::uint64_t>(info->ExceptionRecord->ExceptionAddress), 16);
        appendChar(b, kSectionBufSize, &p, '\n');
        if (code == EXCEPTION_ACCESS_VIOLATION
            && info->ExceptionRecord->NumberParameters >= 2) {
            const ULONG_PTR op   = info->ExceptionRecord->ExceptionInformation[0];
            const ULONG_PTR addr = info->ExceptionRecord->ExceptionInformation[1];
            appendStr(b, kSectionBufSize, &p, "Op   : ");
            appendStr(b, kSectionBufSize, &p,
                      op == 0 ? "read" : op == 1 ? "write" : op == 8 ? "execute" : "?");
            appendStr(b, kSectionBufSize, &p, " @ 0x");
            appendHex64(b, kSectionBufSize, &p, static_cast<std::uint64_t>(addr), 16);
            appendChar(b, kSectionBufSize, &p, '\n');
        }
        writeSection(&cf, b, &p);
    }

    writeContextSection(&cf);
    writeStackTraceWindows(&cf, info->ContextRecord);
    writeFooter(&cf);
    cf.close();
    return true;
}

LONG WINAPI vectoredHandler(EXCEPTION_POINTERS *info)
{
    const DWORD code = info->ExceptionRecord->ExceptionCode;
    if (!isFatalSehCode(code)) return EXCEPTION_CONTINUE_SEARCH;

    bool expected = false;
    if (!g_reporting.compare_exchange_strong(expected, true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    writeReportSeh(info, "Vectored SEH");
    emitStderrHint();
    // Intentionally do NOT reset g_reporting: process is dying. Resetting would
    // let sehTopFilter and the std::terminate path each write duplicate records.
    return EXCEPTION_CONTINUE_SEARCH;  // allow WER / debugger to handle it next
}

LONG WINAPI sehTopFilter(EXCEPTION_POINTERS *info)
{
    bool expected = false;
    if (!g_reporting.compare_exchange_strong(expected, true)) {
        return EXCEPTION_CONTINUE_SEARCH;
    }
    writeReportSeh(info, "Unhandled SEH");
    emitStderrHint();
    return EXCEPTION_CONTINUE_SEARCH;
}

extern "C" void crtSignalHandler(int sig)
{
    bool expected = false;
    if (!g_reporting.compare_exchange_strong(expected, true)) {
        std::_Exit(128 + sig);
    }

    CrashFile cf;
    if (openReport(&cf)) {
        writeHeader(&cf, "CRT signal");
        std::size_t p = 0;
        char *b = g_section_buf;
        appendStr(b, kSectionBufSize, &p, "Sig  : ");
        appendU64(b, kSectionBufSize, &p, static_cast<std::uint64_t>(sig));
        switch (sig) {
            case SIGABRT: appendStr(b, kSectionBufSize, &p, " (SIGABRT)\n"); break;
            case SIGFPE:  appendStr(b, kSectionBufSize, &p, " (SIGFPE)\n");  break;
            case SIGILL:  appendStr(b, kSectionBufSize, &p, " (SIGILL)\n");  break;
            case SIGSEGV: appendStr(b, kSectionBufSize, &p, " (SIGSEGV)\n"); break;
            case SIGTERM: appendStr(b, kSectionBufSize, &p, " (SIGTERM)\n"); break;
            default:      appendStr(b, kSectionBufSize, &p, " (unknown)\n"); break;
        }
        writeSection(&cf, b, &p);
        writeContextSection(&cf);
        writeFooter(&cf);
        cf.close();
    }
    emitStderrHint();
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

#endif // _WIN32

#ifndef _WIN32

const char *posixSignalName(int sig)
{
    switch (sig) {
        case SIGSEGV: return "SIGSEGV";
        case SIGABRT: return "SIGABRT";
        case SIGFPE:  return "SIGFPE";
        case SIGILL:  return "SIGILL";
        case SIGTERM: return "SIGTERM";
#  ifdef SIGBUS
        case SIGBUS:  return "SIGBUS";
#  endif
        default:      return "(unknown)";
    }
}

#  if defined(CRASH_HANDLER_HAVE_BACKTRACE)
void writeStackTracePosix(CrashFile *cf)
{
    void *frames[kMaxStackFrames];
    const int n = backtrace(frames, kMaxStackFrames);
    std::size_t p = 0;
    appendStr(g_section_buf, kSectionBufSize, &p, "Stack (");
    appendU64(g_section_buf, kSectionBufSize, &p, static_cast<std::uint64_t>(n));
    appendStr(g_section_buf, kSectionBufSize, &p, " frames):\n");
    writeSection(cf, g_section_buf, &p);
    // backtrace_symbols_fd is the async-safe variant; backtrace_symbols allocates.
    backtrace_symbols_fd(frames, n, cf->fd);
    cf->flush();
}
#  endif

extern "C" void posixSigaction(int sig, siginfo_t *info, void * /*ucontext*/)
{
    bool expected = false;
    if (!g_reporting.compare_exchange_strong(expected, true)) {
        std::_Exit(128 + sig);
    }

    CrashFile cf;
    if (openReport(&cf)) {
        writeHeader(&cf, "POSIX signal");
        std::size_t p = 0;
        char *b = g_section_buf;
        appendStr(b, kSectionBufSize, &p, "Sig  : ");
        appendU64(b, kSectionBufSize, &p, static_cast<std::uint64_t>(sig));
        appendStr(b, kSectionBufSize, &p, " (");
        appendStr(b, kSectionBufSize, &p, posixSignalName(sig));
        appendStr(b, kSectionBufSize, &p, ")\n");
        if (info && info->si_addr) {
            appendStr(b, kSectionBufSize, &p, "Addr : 0x");
            appendHex64(b, kSectionBufSize, &p,
                        reinterpret_cast<std::uint64_t>(info->si_addr), 16);
            appendChar(b, kSectionBufSize, &p, '\n');
        }
        writeSection(&cf, b, &p);
        writeContextSection(&cf);
#  if defined(CRASH_HANDLER_HAVE_BACKTRACE)
        writeStackTracePosix(&cf);
#  endif
        writeFooter(&cf);
        cf.close();
    }
    emitStderrHint();
    // SA_RESETHAND already restored default action; re-raise so core dump / WER fires.
    ::raise(sig);
    std::_Exit(128 + sig);
}

#endif // !_WIN32

void cppTerminateHandler()
{
    bool expected = false;
    if (!g_reporting.compare_exchange_strong(expected, true)) {
        std::_Exit(3);
    }

    CrashFile cf;
    if (openReport(&cf)) {
        writeHeader(&cf, "std::terminate (unhandled C++ exception)");
        std::size_t p = 0;
        char *b = g_section_buf;
        // current_exception()/rethrow_exception() are NOT signal-safe but this
        // handler runs in normal context (the C++ runtime calls us during stack
        // unwinding completion, not from a signal), so allocation is fine.
        if (std::exception_ptr ep = std::current_exception()) {
            try {
                std::rethrow_exception(ep);
            } catch (const std::exception &e) {
                appendStr(b, kSectionBufSize, &p, "what : ");
                appendStr(b, kSectionBufSize, &p, e.what() ? e.what() : "");
                appendChar(b, kSectionBufSize, &p, '\n');
            } catch (...) {
                appendStr(b, kSectionBufSize, &p, "what : (non-std::exception)\n");
            }
        } else {
            appendStr(b, kSectionBufSize, &p, "what : (no active exception)\n");
        }
        writeSection(&cf, b, &p);
        writeContextSection(&cf);
        writeFooter(&cf);
        cf.close();
    }
    emitStderrHint();
    // Reset guard so the subsequent SIGABRT can write its own (CRT signal /
    // POSIX sigaction) record; that record's UUID/Time differ so they don't
    // conflict. Actually no — we leave the guard set so we don't duplicate.
    std::abort();
}

} // namespace

namespace CrashHandler {

void install()
{
    captureCwdPath();
    captureFallbackPath();
    rotateIfLarge(g_primary_path);
    rotateIfLarge(g_fallback_path);

#ifdef _WIN32
    SymSetOptions(SYMOPT_LOAD_LINES | SYMOPT_UNDNAME | SYMOPT_DEFERRED_LOADS);
    SymInitialize(GetCurrentProcess(), nullptr, TRUE);

    AddVectoredExceptionHandler(1, vectoredHandler);
    SetUnhandledExceptionFilter(sehTopFilter);

    // CRT signals — abort(), FPU exceptions if enabled, etc. VEH covers SEH
    // versions of these but CRT-raised signals don't go through SEH.
    std::signal(SIGABRT, crtSignalHandler);
    std::signal(SIGFPE,  crtSignalHandler);
    std::signal(SIGILL,  crtSignalHandler);
    std::signal(SIGSEGV, crtSignalHandler);
    std::signal(SIGTERM, crtSignalHandler);
#else
    stack_t ss{};
    ss.ss_sp    = g_alt_stack;
    ss.ss_size  = sizeof(g_alt_stack);
    ss.ss_flags = 0;
    sigaltstack(&ss, nullptr);

    struct sigaction sa{};
    sa.sa_sigaction = posixSigaction;
    sa.sa_flags     = SA_ONSTACK | SA_SIGINFO | SA_RESETHAND;
    sigemptyset(&sa.sa_mask);

    sigaction(SIGSEGV, &sa, nullptr);
    sigaction(SIGABRT, &sa, nullptr);
    sigaction(SIGFPE,  &sa, nullptr);
    sigaction(SIGILL,  &sa, nullptr);
    sigaction(SIGTERM, &sa, nullptr);
#  ifdef SIGBUS
    sigaction(SIGBUS,  &sa, nullptr);
#  endif

#  if defined(CRASH_HANDLER_HAVE_BACKTRACE)
    // Warm libgcc unwinder so backtrace() inside a signal handler doesn't lazy-
    // load (which would allocate and possibly deadlock).
    void *warm[8];
    (void)backtrace(warm, 8);
#  endif
#endif

    std::set_terminate(cppTerminateHandler);
}

[[noreturn]] void triggerCrashForTest(const char *kind)
{
    if (kind) {
        if (std::strcmp(kind, "segv") == 0) {
            volatile int *p = nullptr;
            // cppcheck-suppress nullPointer
            *p = 42;  // NOLINT(clang-analyzer-core.NullDereference)
        } else if (std::strcmp(kind, "abrt") == 0) {
            std::abort();
        } else if (std::strcmp(kind, "sof") == 0) {
            struct R {
                void operator()() const {
                    volatile char pad[256];
                    pad[0] = 0;
                    R{}();
                }
            };
            R{}();
        } else if (std::strcmp(kind, "terminate") == 0) {
            throw std::runtime_error("crash handler test: std::runtime_error");
        } else if (std::strcmp(kind, "nonstd") == 0) {
            throw 42;  // NOLINT(hicpp-exception-baseclass)
        } else if (std::strcmp(kind, "div0") == 0) {
            volatile int z = 0;
            // cppcheck-suppress zerodiv
            volatile int x = 1 / z;
            (void)x;
        }
    }
    // If we fall through (unknown kind), force abort so caller sees non-zero exit.
    std::abort();
}

} // namespace CrashHandler
