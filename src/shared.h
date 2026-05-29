// shared.h
// The single header that every .cpp file includes to access global state.
// All variables are defined (allocated) in shared.cpp — the extern declarations
// here just make them visible to the rest of the codebase.
//
// Adding a new global: define it in shared.h (extern) + shared.cpp (definition).

#pragma once

#include "Nexus.h"    // AddonAPI_t and Nexus type definitions
#include "Mumble.h"   // Mumble::Data (shared-memory layout written by GW2)
#include "timer.h"    // Timer class
#include "route.h"    // Route, Checkpoint, RoutePoint, trigger state types
#include "storage.h"  // Split, HistoricalRun, save/load functions
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

// ---------------------------------------------------------------------------
// Nexus / GW2 interface pointers
// ---------------------------------------------------------------------------
extern AddonAPI_t*    APIDefs;    // Nexus API; set in AddonLoad()
extern Mumble::Data*  MumbleLink; // Live GW2 game state (position, map, combat, etc.)

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
extern Timer SpeedrunTimer; // Run timer; paused during load screens
extern Timer GrandTimer;    // Wall-clock timer; includes load screen time

// ---------------------------------------------------------------------------
// Route state
// ---------------------------------------------------------------------------
extern Route       CurrentRoute;
extern float       CameraFOV;            // Camera field of view in radians; kept in sync via identity event
extern std::string CurrentRouteName;     // Display name of the active route
extern std::string CurrentRouteFilepath; // Full path to the loaded .json file; empty if unsaved
extern std::string CurrentHistoryPath;   // Full path to the paired .history file
extern std::string AddonDir;             // Addon base directory (settings, routes, history all live here)

// ---------------------------------------------------------------------------
// UI visibility flags
// ---------------------------------------------------------------------------
extern bool ShowZones;        // Checkpoint zone overlays rendered in the game world
extern float ZoneFadeStart; // Distance at which zones start fading (metres)
extern float ZoneFadeEnd;   // Distance at which zones are fully gone (metres)
extern bool ShowTimer;        // Main speedrun timer overlay
extern bool ShowConfig;       // Route editor / config window
extern bool ShowDebug;        // Debug info (map change log, etc.)
extern bool ShowHistory;      // Run history window
extern bool ShowGrandTotal;   // Extra Grand Total row in the timer table
extern bool ShowRouteBrowser; // Route file browser window

// ---------------------------------------------------------------------------
// TimerMode
// ---------------------------------------------------------------------------
// Controls what each row in the split table shows and how diffs are computed.
//
//   Segment   — each row shows the time for that segment only;
//               diffs compare against the best time for that segment.
//   Split     — each row shows cumulative time from run start;
//               diffs show the overall lead or deficit.
//   LiveSplit — each row shows segment time (like Segment), but diffs show
//               the overall lead or deficit (like Split) — matching the
//               behaviour of the LiveSplit speedrun software.
// ---------------------------------------------------------------------------
enum class TimerMode { Segment = 0, Split = 1, LiveSplit = 2 };
extern TimerMode TimerDisplayMode;

// ---------------------------------------------------------------------------
// Timer display settings
// ---------------------------------------------------------------------------
extern bool CompactMode;    // Single-line timer instead of the full split table
extern int  MaxHistoryRuns; // Cap on how many runs are kept in the history list

// ---------------------------------------------------------------------------
// History / best run
// ---------------------------------------------------------------------------
extern std::vector<Split>         BestRun;     // Splits of the designated best run; drives the diff column
extern std::vector<HistoricalRun> HistoryRuns; // All recorded runs, newest first
// Index of the best run inside HistoryRuns (-1 = none set).
// Stored as a plain int so SaveHistory can write it to the .history file
// without the fragile timestamp-matching that was used previously.
// Updated whenever the best run changes and adjusted after deletions.
extern int BestRunIndex;

// ---------------------------------------------------------------------------
// Per-run state flags
// ---------------------------------------------------------------------------
extern bool   RunFinished;         // Set when a goal trigger fires; cleared by post-run UI actions
extern double DisplayedGrandTotal; // Grand total shown in the overlay; frozen at goal for MapChange runs
extern bool   PendingStart;        // Queued MapChange start; fires once the load screen clears
extern double pendingGrandStop;    // GrandTimer snapshot taken at MapChange goal detection; -1.0 = no snapshot pending.

// ---------------------------------------------------------------------------
// Thread-safety
// ---------------------------------------------------------------------------
// InteractKeyPressed is written by a keybind callback (any thread Nexus chooses)
// and read by AddonRender() on the render thread — atomic prevents data races.
// KeybindMutex guards all other timer mutations shared between AddonRender()
// and the Start / Stop / Reset keybind callbacks.
extern std::atomic<bool> InteractKeyPressed;
extern std::mutex        KeybindMutex;

// ---------------------------------------------------------------------------
// Per-checkpoint runtime trigger state
// All vectors are kept in sync with CurrentRoute.Checkpoints by FullReset()
// and by a size-check at the top of AddonRender() each frame.
// ---------------------------------------------------------------------------
extern CombatTriggerState              CombatStart;         // State machine for the start CombatArena point
extern std::vector<CombatTriggerState> CombatCheckpoints;   // One state machine per intermediate checkpoint
extern CombatTriggerState              CombatGoal;          // State machine for the goal CombatArena point
extern std::vector<bool>               checkpointTriggered; // True once each checkpoint has fired this run
extern bool                            WasInCircleStart;    // Was the player in the start circle last frame?
extern std::vector<bool>               WasInCheckpoint;     // Was the player in each circle checkpoint last frame?

// ---------------------------------------------------------------------------
// FullReset
// ---------------------------------------------------------------------------
// Resets both timers and all per-run trigger state for a clean new attempt.
// Does NOT touch UI flags, display settings, history, or the route definition.
// See shared.cpp for the full list of call sites.
// ---------------------------------------------------------------------------
void FullReset();

// ---------------------------------------------------------------------------
// Hotbar (QuickAccess) hide-all toggle state
// Saved when the player hides all windows, restored when they toggle back.
// ---------------------------------------------------------------------------
extern bool HotbarWindowsHidden;
extern bool HotbarSavedShowTimer;
extern bool HotbarSavedShowConfig;
extern bool HotbarSavedShowHistory;
extern bool HotbarSavedShowZones;
extern bool HotbarSavedShowRouteBrowser;

//Debug
extern float occludePixelRadius;
extern float occludePixelClamp;