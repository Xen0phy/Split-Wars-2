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
    std::string Name;         // display name (from inside the JSON)
    std::string Filepath;     // full absolute path to .json
    std::string HistoryPath;  // full absolute path to sibling .history file
};

struct RouteFolder
{
    std::string              FolderName;
    std::string              FolderPath;   // full filesystem path for this folder
    std::vector<RouteFolder> SubFolders;
    std::vector<RouteFile>   Routes;
};

struct Settings
{
    bool    ShowTimer           = true;
    bool    ShowConfig          = true;
    bool    ShowZones           = true;
    bool    ShowDebug           = false;
    bool    SplitMode           = true;
    bool    CompactMode         = false;
    bool    ShowHistory         = false;
    bool    ShowGrandTotal      = false;
    bool    ShowRouteBrowser    = false;
    int     MaxHistoryRuns      = 10;
};

// Route — no times, safe to share
bool            SaveRoute(const std::string& filepath, const Route& route, const std::string& routeName);
bool            LoadRoute(const std::string& filepath, Route& route, std::string& routeName);

// History — best splits + run history, keyed on the full .history path
bool            SaveHistory(const std::string& historyPath, const std::vector<Split>& bestSplits, const std::vector<HistoricalRun>& runs);
bool            LoadHistory(const std::string& historyPath, std::vector<Split>& bestSplits, std::vector<HistoricalRun>& runs);

// Settings
bool            SaveSettings(const std::string& addonDir, const Settings& settings);
bool            LoadSettings(const std::string& addonDir, Settings& settings);

// Route tree — recursive scan of addonDir
RouteFolder     BuildRouteTree(const std::string& addonDir);

// Utils
std::string     GetAddonDir();
std::string     GetCurrentDateTimeString();
