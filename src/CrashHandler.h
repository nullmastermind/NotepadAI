/*
 * NotepadADE addition: crash reporting to crash_report.txt in the user's
 * Documents folder (with %APPDATA%/NotepadAI/crashes fallback).
 *
 * See CrashHandler.cpp for design notes on async-signal-safety,
 * per-section atomic writes, and the Win32 VEH + POSIX sigaltstack path.
 */

#ifndef CRASHHANDLER_H
#define CRASHHANDLER_H

namespace CrashHandler {

void install();

const char *minidumpPath();

[[noreturn]] void triggerCrashForTest(const char *kind);

} // namespace CrashHandler

#endif // CRASHHANDLER_H
