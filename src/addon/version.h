#pragma once

constexpr int Maj = 1;
constexpr int Min = 5;
constexpr int Bld = 7;
constexpr int Rev = 2;

static constexpr const char* VersionNotice =
    "Welcome! If you're new, I hope you enjoy Split Wars 2.\n\n"
    "New in 1.5.6.0:\n"
    "  * History Maintenance tools to fix up old runs missing a start split\n"
    "  * Fixed the run's start checkpoint not being recorded as a split\n"
    "  * Added a Show Start Split setting for the timer/history\n\n"
    "New in 1.5.5.1:\n"
    "  * Evaluation window\n"
    "    * stacked bar chart of your run history, with hover comparison and pinning\n"
    "  * Max Runs kept is now a per-route setting, not a global one\n"
    "  * Segment records now track 2nd/3rd-best times, shown on hover";