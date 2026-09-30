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

#ifndef TERMINALPROCESSSTATS_H
#define TERMINALPROCESSSTATS_H

#include <QString>
#include <QVector>
#include <QtGlobal>

class TerminalProcessStats
{
public:
    struct ProcessSample {
        quint32 pid = 0;
        quint32 parentPid = 0;
        quint64 cpuTime100ns = 0;
        quint64 rssBytes = 0;
    };

    struct TreeCpuState {
        quint32 rootPid = 0;
        quint64 cpuTime100ns = 0;
        quint64 wall100ns = 0;
    };

    struct TreeStats {
        bool valid = false;
        double cpuPercent = 0.0;
        quint64 rssBytes = 0;
    };

    // One logical CPU's cumulative counters. total includes idle.
    struct ProcessorTick {
        quint64 idle100ns = 0;
        quint64 total100ns = 0;
    };

    static TreeStats aggregateTree(const ProcessSample *samples,
                                   int sampleCount,
                                   quint32 rootPid,
                                   const TreeCpuState *prev,
                                   quint64 nowWall100ns,
                                   int logicalCpus,
                                   TreeCpuState *outState);

    static QString cpuLabel(bool valid, double cpuPercent);
    // Machine load, one logical CPU per line. Empty when there is no sample.
    static QString coreLoadLabel(const double *percent, int count);
    // Open or refresh the tip while the pointer is on the CPU label and the text changed.
    static bool coreTipNeedsPush(bool tipVisible, bool overCpuLabel,
                                 const QString &shown, const QString &next);
    // Idle poll is 1 s. An open CPU tooltip polls fast enough to read as live.
    static int statsPollMs(bool cpuTooltipOpen);
    static void processorLoad(const ProcessorTick *now, int count,
                              const ProcessorTick *prev,
                              double *outPercent);
    static QString ramLabel(bool valid, quint64 rssBytes);

    // One OS process-list pass. Reuses `out` capacity. UI thread only.
    static bool enumerateProcesses(QVector<ProcessSample> &out);
    // One OS processor-load pass. Reuses `out` capacity. UI thread only.
    static bool enumerateProcessorTicks(QVector<ProcessorTick> &out);

    static quint64 nowWall100ns();
    static int logicalCpuCount();
};

#endif
