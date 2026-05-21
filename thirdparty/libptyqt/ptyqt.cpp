/*
 * Adapted from kafeg/ptyqt (MIT). See LICENSE.
 */

#include "ptyqt.h"

#ifdef Q_OS_WIN
#include "conptyprocess.h"
#else
#include "unixptyprocess.h"
#endif

IPtyProcess *PtyQt::createPtyProcess(IPtyProcess::PtyType ptyType)
{
#ifdef Q_OS_WIN
    Q_UNUSED(ptyType);
    return new ConPtyProcess();
#else
    Q_UNUSED(ptyType);
    return new UnixPtyProcess();
#endif
}
