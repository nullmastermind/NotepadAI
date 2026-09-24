/*
 * NotepadADE addition. Windows job object that caps commit charge of a
 * process tree. See LICENSE (libptyqt is MIT).
 */
#ifndef COMMITJOB_H
#define COMMITJOB_H

#include <cstdint>

#ifdef _WIN32

// Job with JOB_OBJECT_LIMIT_JOB_MEMORY and KILL_ON_JOB_CLOSE.
// limitBytes == 0 returns nullptr (caller launches uncapped).
// Does not reserve memory — the limit is a ceiling, not a charge.
// Returns a HANDLE the caller must CloseHandle, or nullptr on failure.
void *createCommitLimitedJob(std::uint64_t limitBytes);

// Assign process (HANDLE) to job (HANDLE). False if either is null or the
// assign fails (process already over the limit, or nested-job rejection).
bool assignProcessToCommitJob(void *job, void *process);

#endif

#endif
