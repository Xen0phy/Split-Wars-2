// storage.h
#pragma once

#include "route.h"
#include "timer.h"
#include <vector>
#include <string>

struct HistoricalRun
{
    std::string         Date;
    double              TotalTime   = 0.0;
    double              GrandTotal  = 0.0;
    std::vector<Split>  Splits;
};

struct RouteFile
{
    std::string Name;
    std::string Filename;
};

struct Settings
{
    bool    ShowTimer       = true;
    bool    ShowConfig      = true;
    bool    ShowZones       = true;
    bool    ShowDebug       = false;
    bool    SplitMode       = true;
    bool    CompactMode     = false;
    bool    ShowHistory     = false;
    bool    ShowGrandTotal  = false;
    int     MaxHistoryRuns  = 10;
};

// Route — no times, safe to share
bool                    SaveRoute(const std::string& addonDir, const Route& route, const std::string& routeName);
bool                    LoadRoute(const std::string& filename, Route& route, std::string& routeName);

// History — best splits + run history, stays local
bool                    SaveHistory(const std::string& addonDir, const std::string& routeName, const std::vector<Split>& bestSplits, const std::vector<HistoricalRun>& runs);
bool                    LoadHistory(const std::string& addonDir, const std::string& routeName, std::vector<Split>& bestSplits, std::vector<HistoricalRun>& runs);

// Settings
bool                    SaveSettings(const std::string& addonDir, const Settings& settings);
bool                    LoadSettings(const std::string& addonDir, Settings& settings);

// Utils
std::vector<RouteFile>  ListRoutes(const std::string& addonDir);
std::string             GetAddonDir();
std::string             GetCurrentDateTimeString();