// shared.h
// The single header that every .cpp file includes to access global state.
// All variables are defined (allocated) in shared.cpp — the extern declarations
// here just make them visible to the rest of the codebase.
//
// Adding a new global: define it in shared.h (extern) + shared.cpp (definition).

#pragma once

#include "Nexus.h"    // AddonAPI_t and Nexus type definitions
#include "Mumble.h"   // Mumble::Data (shared-memory layout written by GW2)
#include "RTAPI.hpp"  // RTAPI::RealTimeData (real-time position and state)
#include "ArcDPS.h"
#include "timer.h"    // Timer class
#include "storage.h"  // Split, HistoricalRun, save/load functions
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

// ---------------------------------------------------------------------------
// Nexus / GW2 interface pointers
// ---------------------------------------------------------------------------
extern AddonAPI_t*    APIDefs;    // Nexus API; set in AddonLoad()
extern Mumble::Data*  MumbleLink; // Mumble shared-memory block; used as fallback data source and for IsMapOpen
extern RTAPI::RealTimeData* RTAPIData;      // Null when RTAPI is not loaded or has been hot-unloaded
extern ArcDPS::PluginInfo* ArcDPSExports; // nullptr; set if this addon ever registers with ArcDPS

// ---------------------------------------------------------------------------
// Addon lifecycle helpers (implemented in addon.cpp and entry.cpp)
// ---------------------------------------------------------------------------
void RegisterKeybinds();      // Registers all keybinds with Nexus (addon.cpp)
void DeregisterKeybinds();    // Deregisters all keybinds from Nexus (addon.cpp)
void SaveCurrentSettings();   // Persists current globals to disk (entry.cpp)
void AddonRender();           // Per-frame render callback registered with Nexus
void AddonOptions();          // Nexus options panel callback

// ---------------------------------------------------------------------------
// Data source
// ---------------------------------------------------------------------------
// Controls which API supplies player position, camera, map ID, and combat state.
// UpdateGameState() reads from the active source each frame and writes into GS.
//
//   Default — use RTAPI if loaded and live, otherwise fall back to Mumble.
//   Mumble  — always use Mumble, even if RTAPI is available.
//   RTAPI   — always use RTAPI; falls back to Mumble if RTAPI is unavailable.
//
// IsMapOpen is always sourced from Mumble regardless of the active source,
// as RTAPI does not expose that flag.
// ---------------------------------------------------------------------------
enum class EDataSource : unsigned char
{
    Default = 0,
    Mumble  = 1,
    RTAPI   = 2
};
extern EDataSource          PreferredSource; // Persisted to settings.json

// ---------------------------------------------------------------------------
// GameState
// ---------------------------------------------------------------------------
// Unified snapshot populated every frame by UpdateGameState().
// All rendering and trigger code reads from this instead of MumbleLink
// or RTAPIData directly, so the rest of the codebase is source-agnostic.
// ---------------------------------------------------------------------------
struct GameState
{
    // Player
    float    PlayerX, PlayerY, PlayerZ;

    // Camera
    float    CameraX, CameraY, CameraZ;
    float    CameraFrontX, CameraFrontY, CameraFrontZ;
    float    FOV;

    // World
    uint32_t MapID;
    bool     IsMapOpen;  // Always from Mumble; false if Mumble is unavailable
    bool     IsLoading;

    // Character
    bool     IsInCombat;

    // Source tracking
    bool        RTAPIAvailable;
    EDataSource ActiveSource;
};

extern GameState GS;

// ---------------------------------------------------------------------------
// UpdateGameState
// ---------------------------------------------------------------------------
// Called once per frame at the top of AddonRender() to populate GS.
// ---------------------------------------------------------------------------
void UpdateGameState();
void SetMumbleFOV(float fov);

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
extern Timer SpeedrunTimer; // Run timer; paused during load screens
extern Timer GrandTimer;    // Wall-clock timer; includes load screen time

// ---------------------------------------------------------------------------
// Route state
// ---------------------------------------------------------------------------
extern Route       CurrentRoute;
extern std::string CurrentRouteName;     // Display name of the active route
extern std::string CurrentRouteFilepath; // Full path to the loaded .json file; empty if unsaved
extern std::string CurrentHistoryPath;   // Full path to the paired .history file
extern std::string AddonDir;             // Addon base directory (settings, routes, history all live here)

// ---------------------------------------------------------------------------
// UI visibility flags
// ---------------------------------------------------------------------------
extern bool ShowZones;        // Checkpoint zone overlays rendered in the game world
extern float ZoneFadeStart;   // Distance at which zones start fading (metres)
extern float ZoneFadeEnd;     // Distance at which zones are fully gone (metres)
extern bool ShowTimer;        // Main speedrun timer overlay
extern bool ShowConfig;       // Route editor / config window
extern bool ShowDebug;        // Debug info window
extern bool ShowHistory;      // Run history window
extern bool ShowGrandTotal;   // Extra Grand Total row in the timer table
extern bool ShowRouteBrowser; // Route file browser window

// ---------------------------------------------------------------------------
// Zone colors
// ---------------------------------------------------------------------------
// RGB components for the start, goal, and intermediate checkpoint zone overlays.
// Defaults match the original hard-coded values.
// ---------------------------------------------------------------------------
extern float ColorStart[3];       // default: { 0.2f, 1.0f, 0.2f }
extern float ColorGoal[3];        // default: { 0.2f, 0.5f, 1.0f }
extern float ColorCheckpoint[3];  // default: { 1.0f, 1.0f, 1.0f }
extern float ColorNull[3];        // deafult: { 1.0f, 0.6f, 0.0f }

// ---------------------------------------------------------------------------
// Time colors
// ---------------------------------------------------------------------------
extern float ColorAhead[3];
extern float ColorBehind[3];
extern float ColorBestRow[3]; // slightly different from ColorAhead since it's a background

// ---------------------------------------------------------------------------
// Window sizes
// ---------------------------------------------------------------------------
extern float ConfigWindowW;
extern float ConfigWindowH;
extern float HistoryWindowW;
extern float HistoryWindowH;
extern float BrowserWindowW;
extern float BrowserWindowH;

// ---------------------------------------------------------------------------
// TimerMode / Timer display settings
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
extern TimerMode    TimerDisplayMode;
extern bool         CompactMode;    // Single-line timer instead of the full split table
extern bool         StreamerMode;
extern std::string  StreamerFontName;
extern int          StreamerFontSize;
extern bool         StreamerShowRunningMillis;
extern int          StreamerHeaderFontSize;
extern bool         ShowCMFill;
extern bool         ShowCMShadow;

// ---------------------------------------------------------------------------
// Crash Mode
// ---------------------------------------------------------------------------
extern bool        CrashMode;
extern float       CMDigitShadowColor[3];
extern float       CMDigitShadowOffset[2];
extern float       CMDigitFillColor[3];
extern float       CMDigitBaseColor[3];
extern float       CMDigitOverlay[3];

// ---------------------------------------------------------------------------
// History / best run
// ---------------------------------------------------------------------------
extern std::vector<Split>         BestRun;      // Splits of the designated best run; drives the diff column
extern std::vector<HistoricalRun> HistoryRuns;  // All recorded runs, newest first
extern int                        MaxHistoryRuns; // Cap on how many runs are kept in the history list
extern int                        BestRunIndex; // Index of the best run in HistoryRuns; -1 = none set
extern std::vector<SegmentRecord> SegmentRecords; // Best times per named Start/End pair

// ---------------------------------------------------------------------------
// Per-run state flags
// ---------------------------------------------------------------------------
extern bool   RunFinished;         // Set when a goal trigger fires; cleared by post-run UI actions
extern double DisplayedGrandTotal; // Grand total shown in the overlay; frozen at goal for MapChange runs
extern bool   PendingStart;        // Queued MapChange start; fires once the load screen clears
extern double pendingGrandStop;    // GrandTimer snapshot at MapChange goal detection; -1.0 = none pending

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
// ---------------------------------------------------------------------------
// Mirrors CurrentRoute.Checkpoints 1-to-1: same indices, same order.
// Rebuilt by FullReset() whenever the route changes.
// Each entry holds the checkpoint's config (Point/Name/IsStart/IsGoal) plus
// its runtime state (wasInCircle, triggered, combat).
// Call cs.ResetRuntime() to zero only the runtime fields without touching config.
// ---------------------------------------------------------------------------
extern std::vector<CheckpointState> CheckpointStates;

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
extern bool HotbarSavedShowConfig;
extern bool HotbarSavedShowHistory;
extern bool HotbarSavedShowRouteBrowser;

// ---------------------------------------------------------------------------
// ArcDPS
// ---------------------------------------------------------------------------
struct KillingBlowEvent {
    uint64_t             ArcTime;
    uint64_t             LocalTime;
    uint64_t             SourceAgent;   // who dealt the killing blow
    uint64_t             DestAgent;     // who died
    char                 SourceName[64];
    char                 DestName[64];  // name of the victim (valid at event time — copy it)
    ArcDPS::EIsFriendFoe IFF;
    bool                 IsLocal;
};
extern std::vector<KillingBlowEvent> KillingBlows;

struct ChangeDeadEvent {
    uint64_t ArcTime;
    uint64_t LocalTime;
    uint64_t AgentID;
    char     Name[64];
    bool     IsLocal;
};
extern std::vector<ChangeDeadEvent> RewardEvents;

struct TargetInfo {
    uintptr_t ID;
    char      Name[64];
};
extern bool             HasTarget;
extern TargetInfo       LastTarget;

struct SquadCombatEntry {
    uintptr_t AgentID;
    char      Name[64];
    uint64_t  ArcTimeEnter;
    uint64_t  LocalTimeEnter;
    uint64_t  ArcTimeExit;
    uint64_t  LocalTimeExit;
    bool      HasExited;
    bool      IsLocal;       // true = LOCAL_RAW, false = SQUAD_RAW
};

extern bool                             InCombat;
extern std::vector<SquadCombatEntry>    CombatEntries;
extern std::mutex                       CombatEntriesMutex;

struct SqCombatStartEvent {
    uint64_t ArcTime;
    uint64_t LocalTime;
    bool     IsLocal;
};
extern std::vector<SqCombatStartEvent> SqCombatStartEvents;

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------
extern float occludePixelRadius; // Base pixel radius for the character occlusion circle
extern float occludePixelClamp;  // Maximum pixel radius the occlusion circle can reach
// Zone render timing — populated by RenderZones() for the selected debug checkpoint.
// Only valid when ShowDebug is true and s_SelectedCheckpoint >= 0.
extern float ZoneRenderAvgMs;
extern bool StreamerMode;extern int   ZoneRenderSelectedIndex; // set by renderer_debug, read by worldrender