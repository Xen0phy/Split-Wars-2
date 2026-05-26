// renderer_shared.cpp
#include "renderer_shared.h"

void FormatTime(char* buf, int bufSize, double elapsed, bool showMillis)
{
    int hours   = (int)(elapsed / 3600);
    int minutes = (int)(elapsed / 60) % 60;
    int seconds = (int)(elapsed) % 60;
    if (showMillis)
    {
        int millis = (int)(elapsed * 1000) % 1000;
        snprintf(buf, bufSize, "%02d:%02d:%02d.%03d", hours, minutes, seconds, millis);
    }
    else
    {
        snprintf(buf, bufSize, "%02d:%02d:%02d", hours, minutes, seconds);
    }
}

// Returns false if the diff should be hidden entirely (far ahead, > 60s).
bool FormatDiff(char* buf, int bufSize, double diff, bool isSplit)
{
    double abs = diff < 0.0 ? -diff : diff;

    if (isSplit)
    {
        // Always show, full format, strip leading zeros
        int minutes = (int)(abs / 60);
        int seconds = (int)(abs) % 60;
        int millis  = (int)(abs * 1000) % 1000;
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

    // Live comparison
    if (diff < -60.0)
        return false;

    if (diff < -10.0)
    {
        int seconds = (int)(abs) % 60;
        snprintf(buf, bufSize, "-%d", seconds);
        return true;
    }

    if (diff < 0.0)
    {
        int seconds = (int)(abs) % 60;
        int millis  = (int)(abs * 1000) % 1000;
        snprintf(buf, bufSize, "-%d.%03d", seconds, millis);
        return true;
    }

    // Behind: full format
    int minutes = (int)(abs / 60);
    int seconds = (int)(abs) % 60;
    int millis  = (int)(abs * 1000) % 1000;
    if (minutes > 0)
        snprintf(buf, bufSize, "+%d:%02d.%03d", minutes, seconds, millis);
    else
        snprintf(buf, bufSize, "+%d.%03d", seconds, millis);
    return true;
}

ImVec4 TimeColor(double current, double best, bool running)
{
    if (running)
        return ImVec4(1.0f, 1.0f, 1.0f, 1.0f);
    if (best <= 0.0)
        return ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
    if (current <= best)
        return ImVec4(0.2f, 1.0f, 0.2f, 1.0f);
    return ImVec4(1.0f, 0.3f, 0.3f, 1.0f);
}

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
    LoadHistory(CurrentHistoryPath, BestRun, HistoryRuns);

    FullReset();
    CurrentRoute.IsValid = true;
}
