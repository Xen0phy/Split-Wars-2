// shared.h
// The single header that every .cpp file includes to access global state.
// All variables are defined (allocated) in shared.cpp — the extern declarations
// here just make them visible to the rest of the codebase.
//
// Adding a new setting: add it to settings_table.h.
// Adding a new non-setting global: extern here + definition in shared.cpp.

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

#define SETTING(S, Key, Type, Default)              extern Type Key;
#define SETTING_ARRAY(S, Key, Size, Defaults)       extern float Key[Size];
#define SETTING_ENUM(S, Key, EnumType, ST, Default) extern EnumType Key;
#define SETTING_STRING(S, Key, Default)             extern std::string Key;
#include "settings_table.h"
#undef SETTING
#undef SETTING_ARRAY
#undef SETTING_ENUM
#undef SETTING_STRING

extern bool ShowSettingsMigrationNotice;

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
// History / best run
// ---------------------------------------------------------------------------
extern std::vector<Split>         BestRun;      // Splits of the designated best run; drives the diff column
extern std::vector<HistoricalRun> HistoryRuns;  // All recorded runs, newest first
extern int                        BestRunIndex; // Index of the best run in HistoryRuns; -1 = none set
extern std::vector<SegmentRecord> SegmentRecords; // Best times per named Start/End pair

// ---------------------------------------------------------------------------
// Fractal Rota
// ---------------------------------------------------------------------------
// Scans HistoryRuns for a run recorded exactly 15 days before today and sets
// it as the comparison reference (BestRun / BestRunIndex).
// No-op if FractalRota is disabled, history is empty, or the oldest run is
// less than 15 days old.  Called once from LoadRouteFile() after history is
// loaded and segments are recalculated.
// ---------------------------------------------------------------------------
void ApplyFractalRota();

// ---------------------------------------------------------------------------
// Per-run state flags
// ---------------------------------------------------------------------------
extern bool   RunFinished;         // Set when a goal trigger fires; cleared by post-run UI actions
extern double DisplayedGrandTotal; // Grand total shown in the overlay; frozen at goal for MapChange runs
extern bool   PendingStart;        // Queued MapChange start; fires once the load screen clears
extern double PendingGrandStop;    // GrandTimer snapshot at MapChange goal detection; -1.0 = none pending

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

struct RewardEvent {
    uint64_t ArcTime;
    uint64_t LocalTime;
    uint64_t AgentID;
    char     Name[64];
    bool     IsLocal;
};

struct LogNpcUpdateEvent {
    uint64_t  ArcTime;
    uint64_t  LocalTime;
    uint64_t  SpeciesID;  // src_agent: species id of the new log boss
    uint64_t  AgentID;    // dst_agent: related agent id
    uint32_t  ServerTime; // value as uint32_t: server unix timestamp
    bool      IsLocal;
};

struct TargetInfo {
    uintptr_t ID;
    char      Name[64];
};

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

struct SqCombatStartEvent {
    uint64_t ArcTime;
    uint64_t LocalTime;
    bool     IsLocal;
};

extern std::vector<KillingBlowEvent>   KillingBlows;
extern std::vector<RewardEvent>        RewardEvents;
extern std::vector<LogNpcUpdateEvent>  LogNpcUpdateEvents;
extern bool                            HasTarget;
extern TargetInfo                      LastTarget;
extern bool                            InCombat;
extern std::vector<SquadCombatEntry>   CombatEntries;
extern std::mutex                      CombatEntriesMutex;
extern std::vector<SqCombatStartEvent> SqCombatStartEvents;

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------
extern bool ShowDebug; // Shows the Debug window
extern float occludePixelRadius; // Base pixel radius for the character occlusion circle
extern float occludePixelClamp;  // Maximum pixel radius the occlusion circle can reach
// Zone render timing — populated by RenderZones() for the selected debug checkpoint.
// Only valid when ShowDebug is true and s_SelectedCheckpoint >= 0.
extern float ZoneRenderAvgMs;
extern int   ZoneRenderSelectedIndex; // set by render_debug, read by worldrender