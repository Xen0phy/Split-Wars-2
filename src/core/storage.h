// storage.h
// Data structures and function declarations for all disk I/O in Split Wars 2.
//
// Three categories of data are persisted:
//   Route    — the checkpoint list for a run, saved as a .json array.
//   History  — completed run records (splits + timestamps) saved as a .history
//              file that lives alongside its .json route file.
//   Settings — UI preferences (window visibility, timer mode, etc.) saved as
//              settings.ini in the addon root directory.
//
// All Save*/Load* functions return false on failure and never throw —
// callers don't need try/catch blocks.

#pragma once

#include "route.h"
#include "timer.h"
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
// SegmentRecord
// ---------------------------------------------------------------------------
// Best time achieved for a named Start/End split pair across all runs.
// A segment is identified by its prefix — "Kinfall" from "Kinfall Start"
// and "Kinfall End". If no matching End exists in a run, that run is skipped.
// The 2nd- and 3rd-best times (and their dates) are tracked alongside the
// best, for display in a hover/tooltip.
// ---------------------------------------------------------------------------
struct SegmentRecord
{
    std::string name;        // Prefix, e.g. "Kinfall"
    double      bestTime;    // Shortest Start→End delta in seconds
    std::string bestDate;    // Date string of the run that achieved bestTime
    double      secondTime  = 0.0; // 2nd-shortest delta, 0.0 if not reached yet
    std::string secondDate;        // Date string of the run that achieved secondTime
    double      thirdTime   = 0.0; // 3rd-shortest delta, 0.0 if not reached yet
    std::string thirdDate;         // Date string of the run that achieved thirdTime
};

// ---------------------------------------------------------------------------
// Split name Start/End suffix helpers
// ---------------------------------------------------------------------------
// The naming convention that ties a segment together — a split literally
// named "X Start" paired with one literally named "X End" — is shared by
// UpdateSegments() below and the evaluation window's span parser
// (render_evaluation.cpp). Both only need to know "is this a Start split"
// and "what End name pairs with it", so that logic lives here once; neither
// caller should re-derive the suffix length or literal text itself.
// ---------------------------------------------------------------------------

// True if name ends in the " Start" suffix.
bool IsStartSplitName(const std::string& name);

// Strips the " Start" suffix, returning the bare segment prefix
// (e.g. "Kinfall Start" -> "Kinfall"). Only valid to call when
// IsStartSplitName(name) is true.
std::string StartSplitPrefix(const std::string& name);

// Builds the matching " End" split name for a given prefix
// (e.g. "Kinfall" -> "Kinfall End").
std::string EndSplitName(const std::string& prefix);

// Recalculates all segment records from scratch across all runs.
// Call on route load to ensure segments are always consistent with history.
void RecalcSegments(const std::vector<HistoricalRun>& runs,
                    std::vector<SegmentRecord>& segments);

// Updates segments with a single new run — faster than full recalc.
// Call immediately after a run finishes and before SaveHistory.
void UpdateSegments(const HistoricalRun& run,
                    std::vector<SegmentRecord>& segments);

// ---------------------------------------------------------------------------
// History I/O
// ---------------------------------------------------------------------------
// SaveHistory — writes all runs and the best-run index to historyPath.
//               bestRunIndex = -1 means no best run is designated.
//               maxHistoryRuns is this route's own trim cap (0 = unlimited).
// LoadHistory — reads historyPath and restores runs and bestRun.
//               bestRun is populated from runs[best_run_index] if valid.
//               outBestIndex is set to the loaded best_run_index value
//               (-1 if none) so callers can track it as a plain integer
//               rather than re-deriving it via timestamp matching later.
//               outMaxHistoryRuns is set to the loaded max_history_runs value,
//               defaulting to 100 for files that predate this field.
// ---------------------------------------------------------------------------
bool SaveHistory(const std::string& historyPath,
                 const std::vector<HistoricalRun>& runs,
                 const std::vector<SegmentRecord>& segments,
                 int bestRunIndex = -1,
                 int maxHistoryRuns = 100);

bool LoadHistory(const std::string& historyPath,
                 std::vector<Split>& bestRun,
                 std::vector<HistoricalRun>& runs,
                 std::vector<SegmentRecord>& segments,
                 int& outBestIndex,
                 int& outMaxHistoryRuns);

// ---------------------------------------------------------------------------
// Settings I/O
// ---------------------------------------------------------------------------
// Both functions operate on addonDir + "\\settings.ini".
// Unknown keys are silently ignored on load, so settings files written by
// older versions load cleanly even if new fields were added since.
// ---------------------------------------------------------------------------
bool SaveSettings(const std::string& addonDir);
bool LoadSettings(const std::string& addonDir);

// ---------------------------------------------------------------------------
// Route tree
// ---------------------------------------------------------------------------
// BuildRouteTree — recursively scans addonDir and returns the root RouteFolder.
//                  Directories sort before files; both groups sort alphabetically.
//                  settings.ini is excluded from the results, as are the
//                  "fonts" and "textures" subdirectories.
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