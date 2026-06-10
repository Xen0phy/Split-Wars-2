// renderer_shared.cpp
// Implements the utility functions shared across all renderer_*.cpp files:
//   • FormatTime    — formats a seconds value into a HH:MM:SS[.mmm] string
//   • FormatDiff    — formats a signed time delta with context-aware precision
//   • TimeColor     — returns the ahead/behind/neutral color for a split time cell
//   • LoadRouteFile — loads a route + its history from disk into the global state

#include "render_shared.h"

// ---------------------------------------------------------------------------
// FormatTime
// ---------------------------------------------------------------------------
// Converts a raw elapsed-seconds value into a human-readable time string.
// With showMillis = true  → "HH:MM:SS.mmm"  (used on the live timer)
// With showMillis = false → "HH:MM:SS"       (used in history/tooltip tables)
// ---------------------------------------------------------------------------
void FormatTime(char* buf, int bufSize, double elapsed, bool showMillis)
{
    int hours   = (int)(elapsed / 3600);
    int minutes = (int)(elapsed / 60) % 60;
    int seconds = (int)(elapsed) % 60;

    if (showMillis)
    {
        int millis = (int)(elapsed * 1000) % 1000;
        if (hours > 0)
            snprintf(buf, bufSize, "%d:%02d:%02d.%03d", hours, minutes, seconds, millis);
        else if (minutes > 0)
            snprintf(buf, bufSize, "%d:%02d.%03d", minutes, seconds, millis);
        else
            snprintf(buf, bufSize, "%d.%03d", seconds, millis);
    }
    else
    {
        if (hours > 0)
            snprintf(buf, bufSize, "%d:%02d:%02d", hours, minutes, seconds);
        else if (minutes > 0)
            snprintf(buf, bufSize, "%d:%02d", minutes, seconds);
        else
            snprintf(buf, bufSize, "%d", seconds);
    }
}

// ---------------------------------------------------------------------------
// FormatTimeCSV
// ---------------------------------------------------------------------------
// Always outputs H:MM:SS.mmm with no leading-zero stripping, so spreadsheet
// applications like Google Sheets auto-recognise the value as a time type.
// e.g. 2.5 seconds → "0:00:02.500"
// ---------------------------------------------------------------------------
void FormatTimeExport(char* buf, int bufSize, double elapsed)
{
    int hours   = (int)(elapsed / 3600);
    int minutes = (int)(elapsed / 60) % 60;
    int seconds = (int)(elapsed) % 60;
    int millis  = (int)(elapsed * 1000) % 1000;
    snprintf(buf, bufSize, "%d:%02d:%02d.%03d", hours, minutes, seconds, millis);
}

// ---------------------------------------------------------------------------
// FormatDiff
// ---------------------------------------------------------------------------
// Formats a signed time delta (current - best) into a compact +/- string.
// Returns false if the diff should be hidden entirely (see below).
//
// Two display modes, controlled by isSplit:
//
//   isSplit = true  (used for completed split rows in the timer)
//     Always shown, full precision, leading zeros stripped.
//     e.g. "-1:02.345" or "-5.123" or "+12.000"
//
//   isSplit = false  (used for the live "ahead/behind" indicator)
//     Applies progressive detail reduction to keep the display clean:
//       • More than 60 s ahead   → hidden entirely (return false); the run is
//         so far ahead that showing a diff would just be noise.
//       • 10–60 s ahead          → whole seconds only, e.g. "-42"
//       • 0–10 s ahead           → seconds + millis,  e.g. "-5.123"
//       • Any amount behind      → full format,        e.g. "+1:02.345"
// ---------------------------------------------------------------------------
bool FormatDiff(char* buf, int bufSize, double diff, bool isSplit, bool isShowMillis)
{
    double abs = diff < 0.0 ? -diff : diff;

    int minutes = (int)(abs / 60);
    int seconds = (int)(abs) % 60;
    int millis  = (int)(abs * 1000) % 1000;

    if(!isShowMillis && !isSplit)
    {
        // Live comparison — hide when more than 60 s ahead to reduce visual clutter.
        if (diff < -60.0)
            return false;

        // 10–60 s ahead: whole seconds only (millis are noise at this margin).
        if (diff < -10.0)
        {
            int seconds = (int)(abs) % 60;
            snprintf(buf, bufSize, "-%d", seconds);
            return true;
        }

        // 0–10 s ahead: seconds + millis for fine-grained feedback.
        if (diff < 0.0)
        {
            int seconds = (int)(abs) % 60;
            int millis  = (int)(abs * 1000) % 1000;
            snprintf(buf, bufSize, "-%d.%03d", seconds, millis);
            return true;
        }
    }
    
    if (isSplit)
    {
        // Completed split — always show with full precision, strip leading zeros.
        if (diff < 0.0)
        {
            if (minutes > 0)
                snprintf(buf, bufSize, "-%d:%02d.%03d", minutes, seconds, millis);
            else
                snprintf(buf, bufSize, "-%d.%03d", seconds, millis);
        }
        else
        {
            if (minutes > 0)
                snprintf(buf, bufSize, "+%d:%02d.%03d", minutes, seconds, millis);
            else
                snprintf(buf, bufSize, "+%d.%03d", seconds, millis);
        }
        return true;
    }
    else {
        if (diff < 0.0)
        {
            if (minutes > 0)
                snprintf(buf, bufSize, "-%d:%02d.%03d", minutes, seconds, millis);
            else
                snprintf(buf, bufSize, "-%d.%03d", seconds, millis);
        }
        else
        {
            if (minutes > 0)
                snprintf(buf, bufSize, "+%d:%02d.%03d", minutes, seconds, millis);
            else
                snprintf(buf, bufSize, "+%d.%03d", seconds, millis);
        }
        return true;
    }
}

// ---------------------------------------------------------------------------
// TimeColor
// ---------------------------------------------------------------------------
// Returns the ImGui color to use when drawing a split time cell:
//   White        — the segment/split is still in progress (running = true)
//   ColorAhead   — no best time to compare against yet, or current <= best
//   ColorBehind  — current time is slower than the best time for this split
// ---------------------------------------------------------------------------
ImVec4 TimeColor(double current, double best, bool running)
{
    if (running)
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f); // White — still running
    if (best <= 0.0)
        return ImVec4(ColorAhead[0], ColorAhead[1], ColorAhead[2], 1.0f); // ColorAhead — no best to beat yet
    if (current <= best)
        return ImVec4(ColorAhead[0], ColorAhead[1], ColorAhead[2], 1.0f); // ColorAhead — ahead of or matched best
    return ImVec4(ColorBehind[0], ColorBehind[1], ColorBehind[2], 1.0f);  // ColorBehind — behind best
}

// ---------------------------------------------------------------------------
// LoadRouteFile
// ---------------------------------------------------------------------------
// Loads a route from disk, replaces the active route and history in global
// state, and resets the timer.  Called from the route browser when the player
// clicks a route entry.
//
// On a successful load:
//   1. The new route and its display name replace CurrentRoute / CurrentRouteName.
//   2. The file paths are updated so subsequent saves go to the right place.
//   3. History is loaded from the paired .history file (BestRun and HistoryRuns).
//   4. FullReset() resyncs all trigger-state arrays to the new route length
//      and clears the timer.
//   5. IsValid is set to true so AddonRender() immediately starts evaluating
//      the route's triggers.
//
// If LoadRoute() fails (file missing, parse error, etc.) we return early and
// leave the previous route untouched.
// ---------------------------------------------------------------------------
void LoadRouteFile(const RouteFile& rf)
{
    Route       newRoute;
    std::string newName;
    if (!LoadRoute(rf.Filepath, newRoute, newName))
        return;

    CurrentRoute         = newRoute;
    CurrentRouteName     = newName;
    CurrentRouteFilepath = rf.Filepath;
    CurrentHistoryPath   = rf.HistoryPath;

    BestRun.clear();
    HistoryRuns.clear();
    BestRunIndex = -1;
    LoadHistory(CurrentHistoryPath, BestRun, HistoryRuns, SegmentRecords, BestRunIndex);
    RecalcSegments(HistoryRuns, SegmentRecords);

    FullReset();
    CurrentRoute.IsValid = true;
}
