// shared.cpp
// Defines (allocates storage for) every global variable declared in shared.h.
// There is exactly one translation unit that defines these — this one.
// All other .cpp files access them via the extern declarations in shared.h.
//
// Also implements FullReset(), the single function that resets all per-run
// state back to a clean slate without touching persistent settings or history.

#include "shared.h"

// ---------------------------------------------------------------------------
// Nexus / GW2 interface pointers
// ---------------------------------------------------------------------------
AddonAPI_t*   APIDefs   = nullptr; // Set in AddonLoad(); used everywhere to call Nexus APIs
Mumble::Data* MumbleLink = nullptr; // Shared-memory block written by GW2 every frame

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
Timer SpeedrunTimer; // Measures the run itself; paused during load screens
Timer GrandTimer;    // Measures wall-clock run time including load screens

// ---------------------------------------------------------------------------
// Route state
// ---------------------------------------------------------------------------
Route       CurrentRoute;                        // The active route being run
float       CameraFOV           = 0.873f;        // Radians (~50°); updated via EV_MUMBLE_IDENTITY_UPDATED
std::string CurrentRouteName    = "New Route";   // Display name shown in the config and history windows
std::string CurrentRouteFilepath;                // Full path to the loaded .json file; empty if unsaved
std::string CurrentHistoryPath;                  // Full path to the paired .history file
std::string AddonDir;                            // Base directory of the addon (where settings/routes live)

// ---------------------------------------------------------------------------
// UI visibility flags
// Each bool controls whether its corresponding ImGui window is drawn.
// ---------------------------------------------------------------------------
bool ShowZones        = true;  // Checkpoint zone overlays in the game world
bool ShowTimer        = true;  // The main speedrun timer overlay
bool ShowConfig       = true;  // The route editor / config window
bool ShowDebug        = false; // Debug info (map change log, etc.)
bool ShowHistory      = false; // Run history window
bool ShowGrandTotal   = false; // Extra Grand Total row in the timer table
bool ShowRouteBrowser = false; // Route file browser window

// ---------------------------------------------------------------------------
// Timer display settings
// ---------------------------------------------------------------------------
TimerMode TimerDisplayMode = TimerMode::Split; // Split / Segment / LiveSplit
bool      CompactMode      = false;            // Single-line timer vs. full split table
int       MaxHistoryRuns   = 10;               // How many runs to keep in the history list

// ---------------------------------------------------------------------------
// History / best run data
// ---------------------------------------------------------------------------
std::vector<Split>         BestRun;     // Splits of the run designated as the best; used for diffs
std::vector<HistoricalRun> HistoryRuns; // All recorded runs for the current route, newest first

// ---------------------------------------------------------------------------
// Per-run state flags
// ---------------------------------------------------------------------------
bool   RunFinished         = false; // Set when a goal trigger fires; cleared after post-run UI actions
double DisplayedGrandTotal = 0.0;   // Grand total shown in the overlay; updated each frame in AddonRender()
bool   PendingStart        = false; // Set on a MapChange start trigger; cleared when the load screen ends

// ---------------------------------------------------------------------------
// Thread-safety
// ---------------------------------------------------------------------------
// InteractKeyPressed is written by the keybind callback (any thread) and read
// by AddonRender() (render thread), so it uses an atomic to avoid data races.
// KeybindMutex guards all other timer mutations that happen in both AddonRender()
// and the Start/Stop/Reset keybind callbacks.
std::atomic<bool> InteractKeyPressed = false;
std::mutex        KeybindMutex;

// ---------------------------------------------------------------------------
// Per-checkpoint runtime trigger state
// These vectors are kept in sync with CurrentRoute.Checkpoints in AddonRender().
// ---------------------------------------------------------------------------
CombatTriggerState              CombatStart;        // State machine for the start CombatArena trigger
std::vector<CombatTriggerState> CombatCheckpoints;  // One state machine per intermediate checkpoint
CombatTriggerState              CombatGoal;         // State machine for the goal CombatArena trigger
std::vector<bool>               checkpointTriggered; // True once a checkpoint has fired this run
bool                            WasInCircleStart = false; // Was the player inside the start circle last frame?
std::vector<bool>               WasInCheckpoint; // Was the player inside each circle checkpoint last frame?

// ---------------------------------------------------------------------------
// FullReset
// ---------------------------------------------------------------------------
// Resets all per-run state to a clean starting point.  Called:
//   • When the player manually resets via keybind or the Reset button.
//   • When a new route is loaded or activated.
//   • When a checkpoint is added or removed in the config window.
//   • Just before a new run starts (automatic triggers).
//
// Does NOT touch: UI visibility flags, display settings, history, or the
// route definition itself — those persist across resets.
//
// The three trigger-state vectors are resized to match the current route
// length here, so if checkpoints were added/removed since the last reset
// the vectors are correctly sized for the new route.
// ---------------------------------------------------------------------------
void FullReset()
{
    SpeedrunTimer.Reset();
    GrandTimer.Reset();
    RunFinished      = false;
    PendingStart     = false;
    WasInCircleStart = false;
    CombatStart      = {};
    CombatGoal       = {};
    CombatCheckpoints.assign(CurrentRoute.Checkpoints.size(), {});
    checkpointTriggered.assign(CurrentRoute.Checkpoints.size(), false);
    WasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
}

// ---------------------------------------------------------------------------
// Hotbar window-hide state
// Saved when the player hides all windows via the hotbar toggle, so the
// previous visibility can be restored when they toggle back.
// ---------------------------------------------------------------------------
bool HotbarWindowsHidden         = false; // True while all windows are hidden by the toggle
bool HotbarSavedShowTimer        = false;
bool HotbarSavedShowConfig       = false;
bool HotbarSavedShowHistory      = false;
bool HotbarSavedShowZones        = false;
bool HotbarSavedShowRouteBrowser = false;