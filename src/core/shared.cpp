// shared.cpp
// Defines (allocates storage for) every global variable declared in shared.h.
// There is exactly one translation unit that defines these — this one.
// All other .cpp files access them via the extern declarations in shared.h.
//
// Also implements FullReset() and UpdateGameState().

#include "shared.h"

// ---------------------------------------------------------------------------
// Nexus / GW2 interface pointers
// ---------------------------------------------------------------------------
AddonAPI_t*   APIDefs             = nullptr; // Set in AddonLoad(); used everywhere to call Nexus APIs
Mumble::Data* MumbleLink          = nullptr; // Mumble shared-memory block; used as fallback and for IsMapOpen
RTAPI::RealTimeData* RTAPIData    = nullptr;
ArcDPS::PluginInfo* ArcDPSExports = nullptr;

// ---------------------------------------------------------------------------
// Settings (persisted to settings.ini via settings_table.h)
// ---------------------------------------------------------------------------
#define SETTING(S, Key, Type, Default)              Type Key = Default;
#define SETTING_ARRAY(S, Key, Size, Defaults)       float Key[Size] = {Defaults};
#define SETTING_ENUM(S, Key, EnumType, ST, Default) EnumType Key = Default;
#define SETTING_STRING(S, Key, Default) std::string Key = Default;
#include "settings_table.h"
#undef SETTING
#undef SETTING_ARRAY
#undef SETTING_ENUM
#undef SETTING_STRING

bool ShowSettingsMigrationNotice = false;

// ---------------------------------------------------------------------------
// GameState
// ---------------------------------------------------------------------------
GameState GS = {};

// ---------------------------------------------------------------------------
// UpdateGameState
// ---------------------------------------------------------------------------
// Populates GS from whichever data source is currently active.
// Called once per frame at the top of AddonRender() before any trigger
// evaluation or rendering reads game state.
//
// Source selection:
//   Default — RTAPI if live, otherwise Mumble.
//   Mumble  — always Mumble.
//   RTAPI   — RTAPI if live, otherwise Mumble.
//
// IsMapOpen is always sourced from Mumble regardless of the active source,
// as RTAPI does not expose that flag.
// IsLoading is derived from RTAPI's GameState enum when RTAPI is active;
// Mumble does not expose a reliable loading flag so it is always false there.
// ---------------------------------------------------------------------------
static float s_MumbleFOV = 0.873f;              // updated by SetMumbleFOV() via the identity event
void SetMumbleFOV(float fov) { s_MumbleFOV = fov; }
void UpdateGameState()
{
    bool rtapiLive = RTAPIData != nullptr && RTAPIData->GameBuild != 0;
    GS.RTAPIAvailable = rtapiLive;

    bool useRTAPI = rtapiLive &&
        (PreferredSource == EDataSource::Default ||
         PreferredSource == EDataSource::RTAPI);

    static unsigned int lastUITick = UINT_MAX;

    if (useRTAPI)
    {
        GS.ActiveSource  = EDataSource::RTAPI;
        GS.PlayerX       = RTAPIData->CharacterPosition[0];
        GS.PlayerY       = RTAPIData->CharacterPosition[1];
        GS.PlayerZ       = RTAPIData->CharacterPosition[2];
        GS.CameraX       = RTAPIData->CameraPosition[0];
        GS.CameraY       = RTAPIData->CameraPosition[1];
        GS.CameraZ       = RTAPIData->CameraPosition[2];
        GS.CameraFrontX  = RTAPIData->CameraFacing[0];
        GS.CameraFrontY  = RTAPIData->CameraFacing[1];
        GS.CameraFrontZ  = RTAPIData->CameraFacing[2];
        GS.FOV           = RTAPIData->CameraFOV;
        GS.MapID         = RTAPIData->MapID;
        GS.IsInCombat    = (uint32_t)RTAPIData->CharacterState &
                           (uint32_t)RTAPI::ECharacterState::IsInCombat;
        GS.IsLoading     = RTAPIData->GameState != RTAPI::EGameState::Gameplay;
        GS.IsMapOpen     = MumbleLink ? MumbleLink->Context.IsMapOpen : false;
    }
    else if (MumbleLink)
    {
        GS.ActiveSource  = EDataSource::Mumble;
        GS.PlayerX       = MumbleLink->AvatarPosition.X;
        GS.PlayerY       = MumbleLink->AvatarPosition.Y;
        GS.PlayerZ       = MumbleLink->AvatarPosition.Z;
        GS.CameraX       = MumbleLink->CameraPosition.X;
        GS.CameraY       = MumbleLink->CameraPosition.Y;
        GS.CameraZ       = MumbleLink->CameraPosition.Z;
        GS.CameraFrontX  = MumbleLink->CameraFront.X;
        GS.CameraFrontY  = MumbleLink->CameraFront.Y;
        GS.CameraFrontZ  = MumbleLink->CameraFront.Z;
        GS.FOV           = s_MumbleFOV;
        GS.MapID         = MumbleLink->Context.MapID;
        GS.IsInCombat    = MumbleLink->Context.IsInCombat;
        GS.IsMapOpen     = MumbleLink->Context.IsMapOpen;
        GS.IsLoading     = (MumbleLink->UITick == lastUITick);
        lastUITick       = MumbleLink->UITick;
    }
}

// ---------------------------------------------------------------------------
// Timers
// ---------------------------------------------------------------------------
Timer SpeedrunTimer; // Measures the run itself; paused during load screens
Timer GrandTimer;    // Measures wall-clock run time including load screens

// ---------------------------------------------------------------------------
// Route state
// ---------------------------------------------------------------------------
Route       CurrentRoute;
std::string CurrentRouteName     = "New Route"; // Display name shown in the config and history windows
std::string CurrentRouteFilepath;               // Full path to the loaded .json file; empty if unsaved
std::string CurrentHistoryPath;                 // Full path to the paired .history file
std::string AddonDir;                           // Base directory of the addon (where settings/routes live)

// ---------------------------------------------------------------------------
// History / best run data
// ---------------------------------------------------------------------------
std::vector<Split>         BestRun;
std::vector<HistoricalRun> HistoryRuns;
int                        BestRunIndex = -1; // Index into HistoryRuns; -1 = none set
std::vector<SegmentRecord> SegmentRecords;

// ---------------------------------------------------------------------------
// Per-run state flags
// ---------------------------------------------------------------------------
bool   RunFinished         = false;
double DisplayedGrandTotal = 0.0;
bool   PendingStart        = false;
double PendingGrandStop    = -1.0; // GrandTimer snapshot at MapChange goal detection; -1.0 = none pending

// ---------------------------------------------------------------------------
// Thread-safety
// ---------------------------------------------------------------------------
std::atomic<bool> InteractKeyPressed = false;
std::mutex        KeybindMutex;

// ---------------------------------------------------------------------------
// Per-checkpoint runtime trigger state
// ---------------------------------------------------------------------------
std::vector<CheckpointState> CheckpointStates;

// ---------------------------------------------------------------------------
// FullReset
// ---------------------------------------------------------------------------
// Resets all per-run state to a clean starting point. Called:
//   • When the player manually resets via keybind or the Reset button.
//   • When a new route is loaded or activated.
//   • When a checkpoint is added or removed in the config window.
//   • Just before a new run starts (automatic triggers).
//
// Does NOT touch: UI visibility flags, display settings, history, or the
// route definition itself — those persist across resets.
//
// CheckpointStates is resized to match the current route length here,
// so if checkpoints were added/removed since the last reset the vector
// is correctly sized for the new route.
// ---------------------------------------------------------------------------
void FullReset()
{
    SpeedrunTimer.Reset();
    GrandTimer.Reset();
    DisplayedGrandTotal = 0.0;
    PendingGrandStop    = -1.0;
    RunFinished         = false;
    PendingStart        = false;

    // Rebuild CheckpointStates from the current route, zeroing all runtime fields.
    // If the route hasn't changed this is equivalent to calling ResetRuntime() on
    // every entry; if checkpoints were added/removed the vector is resized correctly.
    CheckpointStates.resize(CurrentRoute.Checkpoints.size());
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const CheckpointState& src = CurrentRoute.Checkpoints[i];
        CheckpointStates[i].Point   = src.Point;
        CheckpointStates[i].IsStart = src.IsStart;
        CheckpointStates[i].IsGoal  = src.IsGoal;
        strncpy(CheckpointStates[i].Name, src.Name, sizeof(CheckpointStates[i].Name));
        CheckpointStates[i].ResetRuntime();
    }
}

// ---------------------------------------------------------------------------
// Hotbar window-hide state
// Saved when the player hides all windows via the hotbar toggle, so the
// previous visibility can be restored when they toggle back.
// ---------------------------------------------------------------------------
bool HotbarWindowsHidden         = false;
bool HotbarSavedShowConfig       = false;
bool HotbarSavedShowHistory      = false;
bool HotbarSavedShowRouteBrowser = false;

// ---------------------------------------------------------------------------
// ArcDPS
// ---------------------------------------------------------------------------
std::vector<KillingBlowEvent>   KillingBlows;
std::vector<RewardEvent>        RewardEvents;
std::vector<LogNpcUpdateEvent>  LogNpcUpdateEvents;
bool                            HasTarget   = false;
TargetInfo                      LastTarget = {};
bool                            InCombat = false;
std::vector<SquadCombatEntry>   CombatEntries = {};
std::mutex                      CombatEntriesMutex;
std::vector<SqCombatStartEvent> SqCombatStartEvents;

// ---------------------------------------------------------------------------
// ApplyFractalRota
// ---------------------------------------------------------------------------
// Sets the best-run reference to the run from exactly 15 days ago, enabling
// like-for-like comparison across GW2's 15-day fractal daily rotation.
//
// Logic:
//   1. Guard: disabled setting, empty history, or no history path → return.
//   2. Oldest run (last entry, history is newest-first) must be ≥ 15 days old;
//      otherwise the rotation hasn't completed a full cycle yet → return.
//   3. Compute the target date: today's local date minus exactly 15 days.
//   4. Scan HistoryRuns for a run whose date string matches that target date
//      (date-only comparison, "YYYY-MM-DD", ignoring the HH:MM time suffix).
//   5. If found: promote it to BestRun/BestRunIndex (does NOT save to disk —
//      Fractal Rota is a transient selection, not a permanent best-run override).
//   6. If not found: leave BestRun/BestRunIndex unchanged.
//
// Date parsing uses the "YYYY-MM-DD HH:MM" format written by
// GetCurrentDateTimeString().  mktime() normalises out-of-range fields, so
// subtracting 15 from tm_mday works correctly across month and year boundaries.
// ---------------------------------------------------------------------------
void ApplyFractalRota()
{
    if (!FractalRota)             return;
    if (HistoryRuns.empty())      return;
    if (CurrentHistoryPath.empty()) return;

    // --- Step 1: check the oldest run is at least 15 days old ---
    // History is newest-first, so the oldest run is the last entry.
    const std::string& oldestDate = HistoryRuns.back().Date; // "YYYY-MM-DD HH:MM"
    if (oldestDate.size() < 10)   return; // malformed, bail

    // Parse oldest run's date
    struct tm tmOldest = {};
    tmOldest.tm_year  = std::stoi(oldestDate.substr(0, 4)) - 1900;
    tmOldest.tm_mon   = std::stoi(oldestDate.substr(5, 2)) - 1;
    tmOldest.tm_mday  = std::stoi(oldestDate.substr(8, 2));
    tmOldest.tm_isdst = -1;
    time_t tOldest = mktime(&tmOldest);

    // Get today's date at midnight
    time_t tNow = time(nullptr);
    struct tm tmToday = {};
    localtime_s(&tmToday, &tNow);
    tmToday.tm_hour  = 0;
    tmToday.tm_min   = 0;
    tmToday.tm_sec   = 0;
    tmToday.tm_isdst = -1;
    time_t tToday = mktime(&tmToday);

    // Oldest run must be at least 15 days old
    constexpr double SECONDS_PER_DAY = 86400.0;
    double ageDays = difftime(tToday, tOldest) / SECONDS_PER_DAY;
    if (ageDays < 15.0) return;

    // --- Step 2: compute the target date (today − 15 days) ---
    struct tm tmTarget  = tmToday;
    tmTarget.tm_mday   -= 15;
    tmTarget.tm_isdst   = -1;
    mktime(&tmTarget); // normalise (handles month/year rollover)

    char targetDateStr[11]; // "YYYY-MM-DD\0"
    snprintf(targetDateStr, sizeof(targetDateStr), "%04d-%02d-%02d",
        tmTarget.tm_year + 1900,
        tmTarget.tm_mon  + 1,
        tmTarget.tm_mday);

    // --- Step 3: scan for a run on the target date ---
    for (int i = 0; i < (int)HistoryRuns.size(); i++)
    {
        const std::string& runDate = HistoryRuns[i].Date;
        if (runDate.size() < 10) continue;

        // Compare only the "YYYY-MM-DD" prefix, ignoring the " HH:MM" suffix
        if (runDate.compare(0, 10, targetDateStr) == 0)
        {
            BestRun      = HistoryRuns[i].Splits;
            BestRunIndex = i;
            return;
        }
    }
    // No match found — leave BestRun/BestRunIndex as loaded from disk
}

// ---------------------------------------------------------------------------
// Debug
// ---------------------------------------------------------------------------
bool  ShowDebug                = false;
float occludePixelRadius       = 1000.0f; // Base pixel radius for the character occlusion circle
float occludePixelClamp        = 300.0f;  // Maximum pixel radius the occlusion circle can reach
float ZoneRenderAvgMs          = 0.0f;
int   ZoneRenderSelectedIndex  = -1;