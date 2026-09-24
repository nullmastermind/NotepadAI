/*
 * NotepadADE addition. See commitjob.h.
 */
#include "commitjob.h"

#ifdef _WIN32

#include <windows.h>

void *createCommitLimitedJob(std::uint64_t limitBytes)
{
    if (limitBytes == 0) {
        return nullptr;
    }

    HANDLE job = CreateJobObjectW(nullptr, nullptr);
    if (!job) {
        return nullptr;
    }

    // Memory ceiling only. No UI limits — a game launched from the shell must
    // still be able to create a window. No breakaway — children stay in the
    // job so a grandchild (cargo -> game) cannot escape the cap.
    // KILL_ON_JOB_CLOSE reaps the whole tree when the terminal drops the handle,
    // including processes the shell spawned. Closing the job does not free
    // commit until those processes exit; the flag makes them exit.
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
    info.BasicLimitInformation.LimitFlags =
        JOB_OBJECT_LIMIT_JOB_MEMORY | JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    info.JobMemoryLimit = limitBytes;

    if (!SetInformationJobObject(job, JobObjectExtendedLimitInformation, &info, sizeof(info))) {
        CloseHandle(job);
        return nullptr;
    }
    return job;
}

bool assignProcessToCommitJob(void *job, void *process)
{
    if (!job || !process) {
        return false;
    }
    return AssignProcessToJobObject(static_cast<HANDLE>(job), static_cast<HANDLE>(process)) != FALSE;
}

#endif
