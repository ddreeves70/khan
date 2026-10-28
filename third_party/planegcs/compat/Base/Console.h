// SPDX-License-Identifier: LGPL-2.1-or-later
// Khan: stands in for FreeCAD's Base/Console.h and Base/TimeElapsed. PlaneGCS
// only logs debugging text and times its own steps; Khan reports problems as
// diagnostics instead, so the log is dropped.
#pragma once

#include <chrono>

namespace Base
{

struct ConsoleStandIn
{
    template<typename... Args>
    void log(Args&&...)
    {}
    template<typename... Args>
    void warning(Args&&...)
    {}
};

inline ConsoleStandIn& Console()
{
    static ConsoleStandIn console;
    return console;
}

struct TimeElapsed
{
    std::chrono::steady_clock::time_point at = std::chrono::steady_clock::now();
    static double diffTimeF(const TimeElapsed& from, const TimeElapsed& to)
    {
        return std::chrono::duration<double>(to.at - from.at).count();
    }
};

}  // namespace Base
