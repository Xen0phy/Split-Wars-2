// storage.h
// Data structures and function declarations for all disk I/O in Split Wars 2.
//
// Three categories of data are persisted:
//   Route    — the checkpoint list for a run, saved as a .json array.
//   History  — completed run records (splits + timestamps) saved as a .history
//              file that lives alongside its .json route file.
//   Settings — UI preferences (window visibility, timer mode, etc.) saved as
//              settings.json in the addon root directory.
//
// All Save*/Load* functions return false on failure and never throw —
// callers don't need try/catch blocks.

#pragma once

#include "route.h"
#include "timer.h"
#include <vector>
#include <string>

// ---------------------------------------------------------------------------
// HistoricalRun
// ---------------------------------------------------------------------------
// One completed run entry stored in the history list.
//
//   Date       — "YYYY-MM-DD HH:MM" string set when the run finishes.
//   TotalTime  — elapsed run time in seconds, excluding load screens.
//   GrandTotal — wall-clock run time in seconds, including load screens.
//                0.0 for runs recorded before the grand total feature landed.
//   Splits     — ordered list of split timestamps for this run; mirrors the
//                layout of BestRun so the two can be diffed directly.
// ---------------------------------------------------------------------------
struct HistoricalRun
{
    std::string        Date;
    double             TotalTime  = 0.0;
    double             GrandTotal = 0.0;
    std::vector<Split> Splits;
};

// ---------------------------------------------------------------------------
// RouteFile
// ---------------------------------------------------------------------------
// Represents one .json route file found on disk.
// Used by the route browser to populate its tree and by LoadRouteFile() to
// load the route + its paired history into global state.
// ---------------------------------------------------------------------------
struct RouteFile
{
    std::string Name;        // Display name — filename without the .json extension
    std::string Filepath;    // Full absolute path to the .json file
    std::string HistoryPath; // Full absolute path to the sibling .history file
};

// ---------------------------------------------------------------------------
// RouteFolder
// ---------------------------------------------------------------------------
// One node in the route folder tree built by BuildRouteTree().
// The tree mirrors the actual directory structure under the addon directory:
// each sub-directory becomes a child RouteFolder and each .json file becomes
// a RouteFile entry inside it.
//
//   FolderName — directory name shown in the browser ("" for the root node)
//   FolderPath — full filesystem path; used as the drop target for drag-and-drop moves
//   SubFolders — child folders (recursive)
//   Routes     — route files directly inside this folder
// ---------------------------------------------------------------------------
struct RouteFolder
{
    std::string              FolderName;
    std::string              FolderPath;
    std::vector<RouteFolder> SubFolders;
    std::vector<RouteFile>   Routes;
};

// ---------------------------------------------------------------------------
// Settings
// ---------------------------------------------------------------------------
// Flat struct that mirrors the settings.json file.
// Used as an intermediary in AddonLoad/AddonUnload to transfer values between
// the JSON file and the individual global booleans in shared.h:
//   disk → Settings → globals  (ApplySettings on load)
//   globals → Settings → disk  (GatherSettings + SaveSettings on unload/save)
//
// All fields have defaults matching the intended first-run experience.
// TimerDisplayMode integer mapping: 0 = Segment, 1 = Split, 2 = LiveSplit.
// ---------------------------------------------------------------------------
struct Settings
{
    bool ShowTimer        = true;
    bool ShowConfig       = true;
    bool ShowZones        = true;
    float ZoneFadeStart   = 50.0f;
    float ZoneFadeEnd     = 150.0f;
    bool ShowDebug        = false;
    int TimerDisplayMode  = 1;     // Default: Split mode
    bool CompactMode      = false;
    bool ShowHistory      = false;
    bool ShowGrandTotal   = false;
    bool ShowRouteBrowser = false;
    int MaxHistoryRuns    = 10;
    int DataSource        = 0; // 0 = Default, 1 = Mumble, 2 = RTAPI
};

// ---------------------------------------------------------------------------
// Route I/O
// ---------------------------------------------------------------------------
// SaveRoute — serialises route.Checkpoints to a JSON array at filepath.
//             Creates missing parent directories automatically.
// LoadRoute — parses filepath, validates structure and required fields,
//             auto-corrects duplicate start/goal flags, and sets route.IsValid.
//             routeName is set to the filename stem (without extension).
// ---------------------------------------------------------------------------
bool SaveRoute(const std::string& filepath, const Route& route);
bool LoadRoute(const std::string& filepath, Route& route, std::string& routeName);

// ---------------------------------------------------------------------------
// History I/O
// ---------------------------------------------------------------------------
// SaveHistory — writes all runs and the best-run index to historyPath.
//               bestRunIndex = -1 means no best run is designated.
// LoadHistory — reads historyPath and restores runs and bestRun.
//               bestRun is populated from runs[best_run_index] if valid.
//               outBestIndex is set to the loaded best_run_index value
//               (-1 if none) so callers can track it as a plain integer
//               rather than re-deriving it via timestamp matching later.
// ---------------------------------------------------------------------------
bool SaveHistory(const std::string& historyPath, const std::vector<Split>& bestRun,
                 const std::vector<HistoricalRun>& runs, int bestRunIndex = -1);
bool LoadHistory(const std::string& historyPath, std::vector<Split>& bestRun,
                 std::vector<HistoricalRun>& runs, int& outBestIndex);

// ---------------------------------------------------------------------------
// Settings I/O
// ---------------------------------------------------------------------------
// Both functions operate on addonDir + "\\settings.json".
// LoadSettings uses j.value() with defaults throughout so settings files
// written by older versions of the addon load cleanly even if new fields
// were added since.
// ---------------------------------------------------------------------------
bool SaveSettings(const std::string& addonDir, const Settings& settings);
bool LoadSettings(const std::string& addonDir, Settings& settings);

// ---------------------------------------------------------------------------
// Route tree
// ---------------------------------------------------------------------------
// BuildRouteTree — recursively scans addonDir and returns the root RouteFolder.
//                  Directories sort before files; both groups sort alphabetically.
//                  settings.json is excluded from the results.
//                  Returns an empty RouteFolder{} if addonDir doesn't exist.
// ---------------------------------------------------------------------------
RouteFolder BuildRouteTree(const std::string& addonDir);

// ---------------------------------------------------------------------------
// Utilities
// ---------------------------------------------------------------------------
// GetAddonDir            — returns the addon's base directory by locating the
//                          DLL via its own function address (no hard-coded name).
// GetCurrentDateTimeString — returns a "YYYY-MM-DD HH:MM" timestamp string.
// ---------------------------------------------------------------------------
std::string GetAddonDir();
std::string GetCurrentDateTimeString();
