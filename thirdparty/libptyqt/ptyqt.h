/*
 * Adapted from kafeg/ptyqt (MIT). See LICENSE.
 */

#ifndef PTYQT_H
#define PTYQT_H

#include "iptyprocess.h"

class PtyQt
{
public:
    static IPtyProcess *createPtyProcess(IPtyProcess::PtyType ptyType);
};

#endif
