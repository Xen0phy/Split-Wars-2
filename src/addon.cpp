// addon.cpp
// This is the main entry point for the Split Wars 2 Nexus addon.
// It handles:
//   - Registering the addon with the Nexus loader (name, version, author, etc.)
//   - Loading and saving settings on startup/shutdown
//   - Registering all keybinds and their callback functions
//   - The per-frame render loop: loading screen detection, trigger evaluation
//     (start / checkpoint / goal), and dispatching to the individual UI windows
//   - The Nexus options panel (checkboxes, mode buttons, etc.)

#include "renderer_shared.h"
#include "worldrender.h"
#include "hotbar_icon.h"
#include <algorithm>

// The global addon descriptor filled in by GetAddonDef() and handed to Nexus.
AddonDefinition_t AddonDef{};

// Forward declarations — these functions are defined later in this file.
void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();
void HandleIdentityUpdate(void* aEventArgs);

// ---------------------------------------------------------------------------
// GetAddonDef
// ---------------------------------------------------------------------------
// Nexus calls this function when it first discovers the DLL.
// We fill out every field of the AddonDefinition_t struct here so Nexus
// knows who we are, what version we are, and which callbacks to call.
// The function is exported as a plain C symbol so Nexus can find it
// regardless of C++ name mangling.
// ---------------------------------------------------------------------------
extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    AddonDef.Signature          = 0x53573200;           // Unique numeric ID for this addon
    AddonDef.APIVersion         = NEXUS_API_VERSION;    // Nexus API version this addon was built against
    AddonDef.Name               = "Split Wars 2";
    AddonDef.Version.Major      = 0;
    AddonDef.Version.Minor      = 17;
    AddonDef.Version.Build      = 0;
    AddonDef.Version.Revision   = 0;
    AddonDef.Author             = "Xenophy.2716";
    AddonDef.Description        = "A speedrun timer with coordinate-based triggers.";
    AddonDef.Load               = AddonLoad;            // Called once when the addon is loaded
    AddonDef.Unload             = AddonUnload;          // Called once when the addon is unloaded
    AddonDef.Flags              = AF_None;
    AddonDef.Provider           = UP_GitHub;            // Where Nexus should look for updates
    AddonDef.UpdateLink         = "https://github.com/Xen0phy/Split-Wars-2";

    return &AddonDef;
}

// ---------------------------------------------------------------------------
// OnAddonLoaded / OnAddonUnloaded
// ---------------------------------------------------------------------------
// Nexus broadcasts EV_ADDON_LOADED and EV_ADDON_UNLOADED whenever any addon
// is loaded or unloaded at runtime (including after the initial load pass).
// aEventArgs points to the integer signature of the addon that changed.
//
// We listen for RTAPI_SIG specifically so RTAPIData stays valid across
// hot-load/unload cycles — e.g. if the user enables RTAPI after Split Wars 2
// is already running, or disables it mid-session.  Without these handlers,
// RTAPIData would remain a stale pointer after RTAPI unloads.
// ---------------------------------------------------------------------------
void OnAddonLoaded(void* aEventArgs)
{
    int* sig = (int*)aEventArgs;
    if (!sig) return;
    if (*sig == RTAPI_SIG)
        RTAPIData = (RTAPI::RealTimeData*)APIDefs->DataLink_Get(DL_RTAPI);
}

void OnAddonUnloaded(void* aEventArgs)
{
    int* sig = (int*)aEventArgs;
    if (!sig) return;
    if (*sig == RTAPI_SIG)
        RTAPIData = nullptr;
}

// ---------------------------------------------------------------------------
// Keybind callbacks
// Each function below is called by Nexus whenever the matching keybind
// fires.  The aIsRelease parameter is true when the key is released and
// false when it is first pressed.  Most actions should fire on press
// (aIsRelease == false), not on release.
// ---------------------------------------------------------------------------

// "Interact" key — sets a one-frame flag that the trigger system reads to
// detect CircleInteract checkpoint activations.
static void OnInteractKey(const char* aIdentifier, bool aIsRelease)
{
    if (!aIsRelease)
        InteractKeyPressed = true;
}

// "Start/Stop" key — if the timer is already running this acts as a manual
// stop: it adds a final "Manual Stop" split, stops both timers, records the
// run in history, and saves to disk.  If the timer is not running it resets
// everything and starts a fresh run with a "Manual Start" split.
static void OnStartStopKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    std::lock_guard<std::mutex> lock(KeybindMutex); // Prevent racing with AddonRender()
    if (SpeedrunTimer.IsRunning())
    {
        SpeedrunTimer.AddSplit("Manual Stop");
        SpeedrunTimer.Stop();
        GrandTimer.Stop();
        RunFinished = true;

        HistoricalRun run;
        run.Date       = GetCurrentDateTimeString();
        run.TotalTime  = SpeedrunTimer.GetElapsedSeconds();
        run.GrandTotal = GrandTimer.GetElapsedSeconds();
        run.Splits     = SpeedrunTimer.GetSplits();
        HistoryRuns.insert(HistoryRuns.begin(), run);  // Newest run goes to the top
        if (BestRunIndex >= 0)
            BestRunIndex++;
        if ((int)HistoryRuns.size() > MaxHistoryRuns)
            HistoryRuns.resize(MaxHistoryRuns);          // Trim the list to the configured cap
        if (!CurrentHistoryPath.empty())
            SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, BestRunIndex);
    }
    else
    {
        FullReset();
        SpeedrunTimer.Start();
        GrandTimer.Start();
        SpeedrunTimer.AddSplit("Manual Start");
    }
}

// "Checkpoint" key — two different behaviours:
//   • Timer running  → record a manual "Manual Checkpoint" split right now.
//   • Timer stopped  → capture the player's current world position and map ID
//                      and append it as a new checkpoint to the active route.
//                      This is the in-game "place a checkpoint here" workflow.
static void OnCheckpointKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    std::lock_guard<std::mutex> lock(KeybindMutex);
    if (SpeedrunTimer.IsRunning())
    {
        SpeedrunTimer.AddSplit("Manual Checkpoint");
    }
    else if (MumbleLink || GS.RTAPIAvailable)
    {
        Checkpoint cp;
        snprintf(cp.Name, sizeof(cp.Name), "Checkpoint %d", (int)CurrentRoute.Checkpoints.size() + 1);
        cp.Point.X     = GS.PlayerX;
        cp.Point.Y     = GS.PlayerY;
        cp.Point.Z     = GS.PlayerZ;
        cp.Point.MapID = GS.MapID;
        CurrentRoute.Checkpoints.push_back(cp);
    }
}

// Simple toggle keybinds — each one just flips the matching boolean on press.
static void OnShowTimerKey          (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowTimer        = !ShowTimer;        }
static void OnShowConfigKey         (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowConfig       = !ShowConfig;       }
static void OnShowHistoryKey        (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowHistory      = !ShowHistory;      }
static void OnShowZonesKey          (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowZones        = !ShowZones;        }
// Cycles through the three timer display modes (Segment → LiveSplit → Split → …)
static void OnCycleTimerModeKey     (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) TimerDisplayMode = (TimerMode)(((int)TimerDisplayMode + 1) % 3); }
static void OnCompactModeKey        (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) CompactMode      = !CompactMode;      }
static void OnShowRouteBrowserKey   (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowRouteBrowser = !ShowRouteBrowser; }

// "Toggle Hide All Windows" key — used by the hotbar button and its keybind.
// If any Split Wars 2 window is currently visible, hide all of them and save
// which ones were open so they can be restored later.  If nothing is visible,
// restore the previously saved state (or fall back to sensible defaults if
// this is the first press).
static void OnHotbarToggleKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;

    bool anyVisible = ShowTimer || ShowConfig || ShowHistory || ShowZones || ShowRouteBrowser;

    if (anyVisible)
    {
        // Save current visibility and hide everything
        HotbarSavedShowTimer        = ShowTimer;
        HotbarSavedShowConfig       = ShowConfig;
        HotbarSavedShowHistory      = ShowHistory;
        HotbarSavedShowZones        = ShowZones;
        HotbarSavedShowRouteBrowser = ShowRouteBrowser;
        ShowTimer        = false;
        ShowConfig       = false;
        ShowHistory      = false;
        ShowZones        = false;
        ShowRouteBrowser = false;
        HotbarWindowsHidden = true;
    }
    else
    {
        // Restore saved visibility, or fall back to defaults if nothing was saved
        bool nothingToRestore = !HotbarSavedShowTimer && !HotbarSavedShowConfig &&
                                !HotbarSavedShowHistory && !HotbarSavedShowZones &&
                                !HotbarSavedShowRouteBrowser;
        ShowTimer        = nothingToRestore ? true  : HotbarSavedShowTimer;
        ShowConfig       = nothingToRestore ? true  : HotbarSavedShowConfig;
        ShowHistory      = nothingToRestore ? false : HotbarSavedShowHistory;
        ShowZones        = nothingToRestore ? false : HotbarSavedShowZones;
        ShowRouteBrowser = nothingToRestore ? false : HotbarSavedShowRouteBrowser;
        HotbarWindowsHidden = false;
    }
}

// "Reset Timer" key — immediately resets the run without recording history.
static void OnResetTimerKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    std::lock_guard<std::mutex> lock(KeybindMutex);
    FullReset();
}

// ---------------------------------------------------------------------------
// Settings helpers
// These two functions translate between the flat Settings struct (which is
// what gets serialised to disk) and the individual global booleans / enums
// used throughout the rest of the code.
// ---------------------------------------------------------------------------

// Push a loaded Settings object into the global variables.
static void ApplySettings(const Settings& s)
{
    ShowTimer        = s.ShowTimer;
    ShowConfig       = s.ShowConfig;
    ShowZones        = s.ShowZones;
    ZoneFadeEnd      = s.ZoneFadeEnd;
    ZoneFadeStart    = s.ZoneFadeStart;
    ShowDebug        = s.ShowDebug;
    TimerDisplayMode = (TimerMode)s.TimerDisplayMode;
    CompactMode      = s.CompactMode;
    ShowHistory      = s.ShowHistory;
    ShowGrandTotal   = s.ShowGrandTotal;
    ShowRouteBrowser = s.ShowRouteBrowser;
    MaxHistoryRuns   = s.MaxHistoryRuns;
    PreferredSource  = (EDataSource)s.DataSource;
}

// Snapshot the current global variables into a Settings struct ready for saving.
static Settings GatherSettings()
{
    Settings s;
    s.ShowTimer        = ShowTimer;
    s.ShowConfig       = ShowConfig;
    s.ShowZones        = ShowZones;
    s.ZoneFadeStart    = ZoneFadeStart;
    s.ZoneFadeEnd      = ZoneFadeEnd;
    s.ShowDebug        = ShowDebug;
    s.TimerDisplayMode = (int)TimerDisplayMode;
    s.CompactMode      = CompactMode;
    s.ShowHistory      = ShowHistory;
    s.ShowGrandTotal   = ShowGrandTotal;
    s.ShowRouteBrowser = ShowRouteBrowser;
    s.MaxHistoryRuns   = MaxHistoryRuns;
    s.DataSource       = (int)PreferredSource;
    return s;
}

// ---------------------------------------------------------------------------
// AddonQuickAccessMenu
// ---------------------------------------------------------------------------
// Draws the right-click context menu that appears when the player right-clicks
// the Split Wars 2 icon in the Nexus Quick Access bar.  Each item is a
// checkbox-style menu entry that toggles the matching window directly.
// ---------------------------------------------------------------------------
static void AddonQuickAccessMenu()
{
    bool anyVisible = ShowTimer || ShowConfig || ShowHistory || ShowZones || ShowRouteBrowser;

    if (ImGui::MenuItem("Timer",         nullptr, &ShowTimer))        {}
    if (ImGui::MenuItem("Route Config",  nullptr, &ShowConfig))       {}
    if (ImGui::MenuItem("History",       nullptr, &ShowHistory))      {}
    if (ImGui::MenuItem("Route Browser", nullptr, &ShowRouteBrowser)) {}
    if (ImGui::MenuItem("Checkpoints",   nullptr, &ShowZones))        {}
}

// ---------------------------------------------------------------------------
// Keybind registration table
// ---------------------------------------------------------------------------
// All keybinds are listed here in one place.  AddonLoad() and AddonUnload()
// iterate over this array to register / deregister them all in one loop,
// avoiding the need to touch both functions when a new keybind is added.
// ---------------------------------------------------------------------------
using KeybindHandler = void(*)(const char*, bool);
static const struct { const char* ID; KeybindHandler Fn; } Keybinds[] =
{
    { "SW2 Interact",               OnInteractKey         },
    { "SW2 Start/Stop",             OnStartStopKey        },
    { "SW2 Reset Timer",            OnResetTimerKey       },
    { "SW2 Show Timer",             OnShowTimerKey        },
    { "SW2 Show Route Browser",     OnShowRouteBrowserKey },
    { "SW2 Show Route Config",      OnShowConfigKey       },
    { "SW2 Show Route History",     OnShowHistoryKey      },
    { "SW2 Cycle Timer Mode",       OnCycleTimerModeKey   },
    { "SW2 Toggle Compact Mode",    OnCompactModeKey      },
    { "SW2 Add/Call Checkpoint",    OnCheckpointKey       },
    { "SW2 Show Zones",             OnShowZonesKey        },
    { "SW2 Toggle Hide All Windows",OnHotbarToggleKey     },
};

// ---------------------------------------------------------------------------
// AddonLoad
// ---------------------------------------------------------------------------
// Called once by Nexus after the DLL is loaded.  This is where we do all
// one-time setup:
//   1. Store the API pointer and hook into ImGui's allocator so our ImGui
//      calls share the same heap as the host process.
//   2. Grab the MumbleLink shared-memory pointer (always-present fallback
//      for position, map, FOV, and IsMapOpen).
//   3. Attempt to grab the RTAPI data pointer (optional higher-accuracy
//      source; null if RTAPI is not installed).
//   4. Subscribe to EV_ADDON_LOADED/UNLOADED to keep RTAPIData valid if
//      RTAPI is hot-loaded or unloaded mid-session.
//   5. Load persisted settings from disk if a settings file exists.
//   6. Register the render and options callbacks with Nexus.
//   7. Subscribe to EV_MUMBLE_IDENTITY_UPDATED (for camera FOV changes).
//   8. Upload the hotbar icon textures and add the Quick Access button.
//   9. Register all keybinds.
// ---------------------------------------------------------------------------
void AddonLoad(AddonAPI_t* aApi)
{
    APIDefs = aApi;

    // Point ImGui at the host's context and memory allocator so all ImGui
    // state is shared — without this, font atlases and style settings would
    // be duplicated and potentially crash.
    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
        (void(*)(void*, void*))APIDefs->ImguiFree
    );

    // MumbleLink is a shared-memory block that GW2 writes every frame with
    // player position, map ID, combat state, and more.  It is always the
    // fallback data source and the sole source for IsMapOpen and FOV.
    MumbleLink = (Mumble::Data*)APIDefs->DataLink_Get("DL_MUMBLE_LINK");

    // RTAPIData is an optional higher-accuracy data source provided by the
    // RTAPI addon.  Null if RTAPI is not installed or not yet loaded.
    // UpdateGameState() selects the active source each frame based on
    // PreferredSource and whether RTAPIData is non-null.
    RTAPIData = (RTAPI::RealTimeData*)APIDefs->DataLink_Get(DL_RTAPI);

    // Subscribe to addon lifecycle events so RTAPIData is kept in sync if
    // RTAPI is loaded or unloaded after Split Wars 2 is already running.
    APIDefs->Events_Subscribe("EV_ADDON_LOADED",   OnAddonLoaded);
    APIDefs->Events_Subscribe("EV_ADDON_UNLOADED", OnAddonUnloaded);
    AddonDir   = GetAddonDir();

    // Apply persisted settings if a settings file is found; otherwise the
    // compiled-in defaults from shared.h remain active.
    Settings s;
    if (LoadSettings(AddonDir, s))
        ApplySettings(s);

    // Register the per-frame render callback and the Nexus options panel callback.
    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);

    // Subscribe to identity updates so we can keep CameraFOV in sync.
    APIDefs->Events_Subscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityUpdate);

    // Load hotbar icon textures from embedded memory and register QuickAccess shortcut.
    // The icon images are baked into hotbar_icon.h as byte arrays at compile time.
    APIDefs->Textures_GetOrCreateFromMemory(
        "TEX_SW2_HOTBAR",
        (void*)g_HotbarIconData,
        g_HotbarIconData_size
    );
    APIDefs->Textures_GetOrCreateFromMemory(
        "TEX_SW2_HOTBAR_HOVER",
        (void*)g_HotbarIconHoverData,
        g_HotbarIconHoverData_size
    );
    // The Quick Access button uses the hotbar toggle keybind action so a left-click
    // on the icon triggers the same show/hide behaviour as the keybind.
    APIDefs->QuickAccess_Add(
        "QA_SW2_HIDE_TOGGLE",
        "TEX_SW2_HOTBAR",
        "TEX_SW2_HOTBAR_HOVER",
        "SW2 Toggle Hide All Windows",
        "Split Wars 2: Hide/Restore Windows"
    );
    APIDefs->QuickAccess_AddContextMenu("QA_SW2_CTXMENU", "QA_SW2_HIDE_TOGGLE", AddonQuickAccessMenu);

    // Register all keybinds defined in the table above.
    for (auto& kb : Keybinds)
    APIDefs->InputBinds_RegisterWithStruct(kb.ID, kb.Fn, Keybind_t{});

    APIDefs->Log(LOGL_INFO, "Split Wars 2", "Split Wars 2 loaded.");
}

// ---------------------------------------------------------------------------
// AddonUnload
// ---------------------------------------------------------------------------
// Called by Nexus just before the DLL is unloaded (e.g. on game exit or when
// the user disables the addon).  We mirror everything done in AddonLoad:
// save settings, deregister all keybinds, remove the Quick Access button,
// and unsubscribe from all events (including EV_ADDON_LOADED/UNLOADED for
// RTAPI lifecycle tracking) and render callbacks so no dangling function
// pointers remain inside Nexus after our DLL is gone.
// ---------------------------------------------------------------------------
void AddonUnload()
{
    SaveSettings(AddonDir, GatherSettings());
    
    // Deregister all keybinds.
    for (auto& kb : Keybinds)
        APIDefs->InputBinds_Deregister(kb.ID);

    APIDefs->QuickAccess_Remove("QA_SW2_HIDE_TOGGLE");
    APIDefs->QuickAccess_RemoveContextMenu("QA_SW2_CTXMENU");
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);

    // Unsubscribe all event listeners registered in AddonLoad.
    APIDefs->Events_Unsubscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityUpdate);
    APIDefs->Events_Unsubscribe("EV_ADDON_LOADED",            OnAddonLoaded);
    APIDefs->Events_Unsubscribe("EV_ADDON_UNLOADED",          OnAddonUnloaded);

    APIDefs->Log(LOGL_INFO, "Split Wars 2", "Split Wars 2 unloaded.");
}

// ---------------------------------------------------------------------------
// HandleIdentityUpdate
// ---------------------------------------------------------------------------
// Nexus fires this event whenever the player's identity changes (character
// swap, FOV slider, etc.).  We only care about FOV so we can pass it on to
// the world-space overlay renderer (worldrender.cpp).
// ---------------------------------------------------------------------------
void HandleIdentityUpdate(void* aEventArgs)
{
    if (!aEventArgs) return;
    Mumble::Identity* identity = (Mumble::Identity*)aEventArgs;
    CameraFOV = identity->FOV;
}

// ---------------------------------------------------------------------------
// PointTriggered  (file-private helper)
// ---------------------------------------------------------------------------
// Checks whether a single RoutePoint was triggered this frame given the
// player's previous and current position/map.  The check depends on the
// trigger type configured for that point:
//
//   Plane          — the player's movement vector crossed the trigger plane
//   MapChange      — the player was on the configured map and just left it
//   CircleInteract — the player is inside the radius AND pressed Interact
//   Circle (default) — the player is inside the radius
// ---------------------------------------------------------------------------
static bool PointTriggered(const Vector3& prevPos, const Vector3& currPos,
                            unsigned int prevMapID, unsigned int currMapID,
                            const RoutePoint& point)
{
    switch (point.TriggerType)
    {
        case ETriggerType::Plane:
            return HasCrossedPlane(prevPos, currPos, point);
        case ETriggerType::MapChange:
            return prevMapID == point.MapID && currMapID != point.MapID;
        case ETriggerType::CircleInteract:
            return IsWithinRange(currPos, point) && InteractKeyPressed;
        case ETriggerType::Circle:
        default:
            return IsWithinRange(currPos, point);
    }
}

// ---------------------------------------------------------------------------
// AddonRender
// ---------------------------------------------------------------------------
// Called every frame by Nexus. Handles loading screen detection, all
// start/checkpoint/goal trigger logic, and dispatches to the individual
// render windows. Timer mutations are guarded by KeybindMutex so this
// function and the keybind callbacks can't race each other.
// ---------------------------------------------------------------------------
void AddonRender()
{
    if (!MumbleLink && !GS.RTAPIAvailable) return;
    UpdateGameState(); // populate GS from whichever source is active

    // --- Per-frame state (persists between calls via static locals) ---
    static unsigned int lastUITick       = 0;
    static bool         wasLoading       = false;
    static Vector3      prevPos          = {0, 0, 0};
    static unsigned int prevMapID        = 0;
    static bool         prevInCombat     = false;
    static std::chrono::steady_clock::time_point loadScreenStart;

    // Snapshot current game state for this frame.
    Vector3      currPos      = Vector3{GS.PlayerX, GS.PlayerY, GS.PlayerZ};
    unsigned int currMapID    = GS.MapID;
    bool         currInCombat = GS.IsInCombat;

    // -------------------------------------------------------------------------
    // CombatArena trigger helper (lambda, defined inline for access to frame state)
    //
    // Advances a CombatTriggerState one frame and returns true when the
    // combat segment is considered finished.  A segment is "finished" when:
    //   • The player leaves the zone while still in combat, OR
    //   • The player drops combat inside the zone and a 2-second grace period
    //     expires without combat resuming (catches brief combat drops mid-fight)
    //
    // The state machine has two states: Armed (in combat) and GracePending
    // (combat dropped, timer running until either re-enter combat or grace expires).
    // -------------------------------------------------------------------------
    auto TickCombat = [&](CombatTriggerState& cs, const RoutePoint& point,
                          bool onCorrectMap, bool inCircle) -> bool
    {
        constexpr double GraceDuration = 2.0; // seconds to wait after combat drops

        if (cs.finished) return false;

        // Not yet active — wait for a rising combat edge inside the zone.
        if (!cs.active)
        {
            bool risingEdge = currInCombat && !prevInCombat;
            if (risingEdge && onCorrectMap && inCircle)
            {
                cs.active = true;
                cs.state  = ECombatState::Armed;
            }
            return false;
        }

        // Armed: player is in combat inside the zone.
        if (cs.state == ECombatState::Armed)
        {
            // Left the zone while still in combat — trigger immediately.
            if (!inCircle)
            {
                cs.active   = false;
                cs.finished = true;
                return true;
            }
            // Dropped combat while still in zone — start grace period.
            if (!currInCombat)
            {
                cs.state      = ECombatState::GracePending;
                cs.dropTime   = SpeedrunTimer.GetElapsedSeconds();
                cs.graceStart = std::chrono::steady_clock::now();
            }
            return false;
        }

        // GracePending: combat dropped, waiting to see if it comes back.
        else if (cs.state == ECombatState::GracePending)
        {
            // Still out of combat and inside the zone — check if grace period expired.
            if (!currInCombat && inCircle)
            {
                auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - cs.graceStart).count();
                if (elapsed >= GraceDuration)
                {
                    // Grace period expired — combat segment is done.
                    cs.active   = false;
                    cs.finished = true;
                    return true;
                }
                return false;
            }

            // Re-entered combat before grace expired — go back to Armed.
            if (currInCombat && inCircle)
            {
                cs.state    = ECombatState::Armed;
                cs.dropTime = 0.0;
                return false;
            }

            // Left the zone during grace period — trigger immediately.
            cs.active   = false;
            cs.finished = true;
            return true;
        }

        return false;
    };

    // -------------------------------------------------------------------------
    // Loading screen detection
    // GS.IsLoading is set by UpdateGameState() each frame. With RTAPI it
    // reflects GameState != Gameplay directly; with Mumble it is derived from
    // UITick stalling (UITick stops incrementing during load screens).
    // -------------------------------------------------------------------------
    bool isLoading = GS.IsLoading;

    // Convenience pointers to the designated start and goal checkpoints.
    Checkpoint* startCp = GetStart(CurrentRoute);
    Checkpoint* goalCp  = GetGoal(CurrentRoute);

    // -------------------------------------------------------------------------
    // Main logic — held under KeybindMutex so timer calls here and in keybind
    // callbacks (Start/Stop/Reset) don't race each other.
    // -------------------------------------------------------------------------
    {
        std::lock_guard<std::mutex> lock(KeybindMutex);

            // --- Pause / resume the speedrun timer around load screens ---
            if (isLoading && !wasLoading)
            {
                SpeedrunTimer.Pause();

                // Special case for MapChange goals: if the goal fires on the map
                // transition itself, snapshot the GrandTimer now (before the map
                // actually changes) so load-screen time is excluded from the grand total.
                if (SpeedrunTimer.IsRunning() && goalCp &&
                    goalCp->Point.TriggerType == ETriggerType::MapChange &&
                    goalCp->Point.MapID != 0 &&
                    currMapID == goalCp->Point.MapID)
                {
                    pendingGrandStop = GrandTimer.GetElapsedSeconds();
                }

                if (ShowDebug)
                {
                    loadScreenStart = std::chrono::steady_clock::now();
                    APIDefs->Log(LOGL_DEBUG, "Split Wars 2", "Load screen started — SpeedrunTimer paused.");
                }
            }
            else if (!isLoading && wasLoading)
            {
                SpeedrunTimer.Resume();

                // If we didn't actually leave the goal map (e.g. a mid-run load
                // screen on the same map), discard the snapshot.
                if (pendingGrandStop >= 0.0 && goalCp && currMapID == goalCp->Point.MapID)
                    pendingGrandStop = -1.0;

                if (ShowDebug)
                {
                    double loadDuration = std::chrono::duration<double>(
                        std::chrono::steady_clock::now() - loadScreenStart).count();
                    std::string msg = "Load screen ended — SpeedrunTimer resumed. Load screen duration: " 
                        + std::to_string(loadDuration) + "s";
                    APIDefs->Log(LOGL_DEBUG, "Split Wars 2", msg.c_str());
                }
            }

        if (CurrentRoute.IsValid)
        {
            // Keep the parallel trigger-state arrays in sync with the route length.
            // These arrays track per-checkpoint state: was the player inside last
            // frame, has the checkpoint already fired this run, and combat state.
            if (WasInCheckpoint.size() != CurrentRoute.Checkpoints.size())
            {
                WasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
                checkpointTriggered.assign(CurrentRoute.Checkpoints.size(), false);
                CombatCheckpoints.assign(CurrentRoute.Checkpoints.size(), {});
            }

            // --- Start trigger logic ---
            if (startCp)
            {
                RoutePoint& startPt = startCp->Point;

                // Circle start: reset when the player enters the zone, then
                // start the timer when they leave it.  This lets the player
                // stand in the start zone to reset and walk out to begin.
                if (startPt.TriggerType == ETriggerType::Circle)
                {
                    bool onCorrectMap = startPt.MapID == 0 || currMapID == startPt.MapID;
                    bool inStart = onCorrectMap && IsWithinRange(currPos, startPt);

                    if (inStart && !WasInCircleStart && !SpeedrunTimer.IsRunning())
                        FullReset();

                    if (!inStart && WasInCircleStart &&
                        !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
                    {
                        SpeedrunTimer.Start();
                        GrandTimer.Start();
                    }

                    WasInCircleStart = inStart;
                }
                // Plane start: start the timer the moment the player crosses the plane.
                else if (startPt.TriggerType == ETriggerType::Plane)
                {
                    bool onCorrectMap = startPt.MapID == 0 || currMapID == startPt.MapID;
                    bool crossed = onCorrectMap && HasCrossedPlane(prevPos, currPos, startPt);

                    if (crossed && !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
                    {
                        FullReset();
                        SpeedrunTimer.Start();
                        GrandTimer.Start();
                    }
                }
                // MapChange start: the trigger fires on the map-transition event, but the
                // actual start is deferred (PendingStart flag) until the load screen clears
                // so the timer begins at the moment gameplay resumes, not mid-load.
                else if (startPt.TriggerType == ETriggerType::MapChange)
                {
                    bool justLeft = (prevMapID == startPt.MapID) && (currMapID != startPt.MapID);
                    if (startPt.MapID != 0 && justLeft &&
                        !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
                    {
                        PendingStart = true;
                    }
                }
                // CircleInteract start: start when the player presses Interact inside the zone.
                else if (startPt.TriggerType == ETriggerType::CircleInteract)
                {
                    bool onCorrectMap = startPt.MapID == 0 || currMapID == startPt.MapID;
                    bool inCircle = onCorrectMap && IsWithinRange(currPos, startPt);

                    if (inCircle && InteractKeyPressed && !SpeedrunTimer.IsRunning())
                    {
                        FullReset();
                        SpeedrunTimer.Start();
                        GrandTimer.Start();
                    }
                }
                // CombatArena start: start on a rising combat edge (not-in-combat → in-combat)
                // while the player is inside the configured zone.
                else if (startPt.TriggerType == ETriggerType::CombatArena)
                {
                    bool onCorrectMap = startPt.MapID == 0 || currMapID == startPt.MapID;
                    bool inCircle = onCorrectMap && IsWithinRange(currPos, startPt);

                    // Once a previous combat segment fully resolved, re-arm so the player
                    // can start another run without a manual reset.
                    if (CombatStart.finished && !SpeedrunTimer.IsRunning())
                        CombatStart = {};

                    // Detect whether start and goal are the exact same zone.
                    // If so, the goal tracker needs to be armed at the same time as the
                    // start tracker (since both share one physical combat area).
                    bool sameArea = goalCp &&
                        goalCp->Point.TriggerType == ETriggerType::CombatArena &&
                        goalCp->Point.MapID  == startPt.MapID  &&
                        goalCp->Point.X      == startPt.X      &&
                        goalCp->Point.Y      == startPt.Y      &&
                        goalCp->Point.Z      == startPt.Z      &&
                        goalCp->Point.Radius == startPt.Radius;

                    if (!SpeedrunTimer.IsRunning())
                    {
                        bool risingEdge = currInCombat && !prevInCombat;
                        if (risingEdge && onCorrectMap && inCircle)
                        {
                            FullReset();
                            SpeedrunTimer.Start();
                            GrandTimer.Start();
                            CombatStart.active = true;
                            CombatStart.state  = ECombatState::Armed;

                            // For single-arena runs, arm the goal tracker at the same time.
                            if (sameArea)
                            {
                                CombatGoal.active = true;
                                CombatGoal.state  = ECombatState::Armed;
                            }
                        }
                    }
                    else if (SpeedrunTimer.IsRunning() && CombatStart.active)
                    {
                        // Separate start/goal zones: tick the start combat tracker
                        // and record a "Combat End" split when the start-zone fight finishes.
                        if (!sameArea)
                        {
                            bool finished = TickCombat(CombatStart, startPt, onCorrectMap, inCircle);
                            if (finished)
                            {
                                double t = CombatStart.dropTime > 0.0
                                    ? CombatStart.dropTime
                                    : SpeedrunTimer.GetElapsedSeconds();
                                Split s;
                                snprintf(s.Name, sizeof(s.Name), "%s Combat End", startCp->Name);
                                s.Timestamp = t;
                                SpeedrunTimer.AddSplitAt(s);
                            }
                        }
                    }
                }
            }

            // --- Checkpoint and goal logic (skipped during load screens) ---
            if (!isLoading && SpeedrunTimer.IsRunning())
            {
                for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
                {
                    // The start and goal are handled by their own dedicated blocks above/below.
                    if (CurrentRoute.Checkpoints[i].IsStart || CurrentRoute.Checkpoints[i].IsGoal)
                        continue;

                    const RoutePoint& cp = CurrentRoute.Checkpoints[i].Point;

                    // MapChange checkpoints don't need to be on a specific map (the
                    // trigger is the act of leaving), so we skip the map filter for them.
                    bool onCorrectMap = cp.TriggerType == ETriggerType::MapChange ||
                                        cp.MapID == 0 ||
                                        currMapID == cp.MapID;

                    bool triggered = false;
                    if (onCorrectMap)
                    {
                        if (cp.TriggerType == ETriggerType::CombatArena)
                        {
                            // CombatArena checkpoints record a "Combat Start" split when
                            // the player first enters combat in the zone, and a "Combat End"
                            // split when the combat segment finishes.
                            bool inCircle = IsWithinRange(currPos, cp);
                            if (!CombatCheckpoints[i].finished)
                            {
                                if (!CombatCheckpoints[i].active)
                                {
                                    bool risingEdge = currInCombat && !prevInCombat;
                                    if (risingEdge && inCircle)
                                    {
                                        CombatCheckpoints[i] = { true, ECombatState::Armed };
                                        char splitStartName[68];
                                        snprintf(splitStartName, sizeof(splitStartName), "%s Combat Start", CurrentRoute.Checkpoints[i].Name);
                                        SpeedrunTimer.AddSplit(splitStartName);
                                    }
                                }
                                else
                                {
                                    triggered = TickCombat(CombatCheckpoints[i], cp, onCorrectMap, inCircle);
                                }
                            }
                        }
                        else
                        {
                            triggered = PointTriggered(prevPos, currPos, prevMapID, currMapID, cp);
                        }
                    }

                    // Record the split.
                    // Circle triggers require the player to have been *outside* the zone
                    // last frame (WasInCheckpoint[i] == false) to prevent the split from
                    // firing again the moment the player re-enters an already-passed circle.
                    if (triggered && !checkpointTriggered[i] &&
                        (cp.TriggerType != ETriggerType::Circle || !WasInCheckpoint[i]))
                    {
                        if (cp.TriggerType == ETriggerType::CombatArena &&
                            CombatCheckpoints[i].dropTime > 0.0)
                        {
                            // Back-date the split to when combat actually dropped.
                            Split s;
                            snprintf(s.Name, sizeof(s.Name), "%s Combat End", CurrentRoute.Checkpoints[i].Name);
                            s.Timestamp = CombatCheckpoints[i].dropTime;
                            SpeedrunTimer.AddSplitAt(s);
                        }
                        else
                        {
                            SpeedrunTimer.AddSplit(CurrentRoute.Checkpoints[i].Name);
                        }
                        checkpointTriggered[i] = true;
                    }

                    // Track whether the player was inside a circle zone last frame.
                    // Only circle types use this flag; other types reset it to false.
                    WasInCheckpoint[i] = (cp.TriggerType == ETriggerType::Circle && onCorrectMap)
                        ? IsWithinRange(currPos, cp)
                        : false;
                }

                // --- Goal trigger ---
                bool goalTriggered = false;
                double goalTime    = 0.0;
                if (goalCp)
                {
                    RoutePoint& goalPt = goalCp->Point;

                    // AllCheckpoints goal: fires once every non-start/non-goal checkpoint
                    // has been triggered at least once this run.
                    // NOTE: AllCheckpoints fires immediately if there are no intermediate checkpoints.
                    // This is intentional — "all zero checkpoints have been triggered" is vacuously true.
                    // A route with only a start and an AllCheckpoints goal is simply a broken route.
                    // Use a Circle or Plane goal instead if you want a two-point run.
                    if (goalPt.TriggerType == ETriggerType::AllCheckpoints)
                    {
                        bool allDone = !CurrentRoute.Checkpoints.empty();
                        for (int i = 0; i < (int)checkpointTriggered.size(); i++)
                        {
                            if (CurrentRoute.Checkpoints[i].IsStart || CurrentRoute.Checkpoints[i].IsGoal)
                                continue;
                            if (!checkpointTriggered[i]) { allDone = false; break; }
                        }
                        goalTriggered = allDone;
                    }
                    // CombatArena goal: tick the combat tracker and stop the run when
                    // the goal combat segment finishes.
                    else if (goalPt.TriggerType == ETriggerType::CombatArena)
                    {
                        bool onCorrectMap = goalPt.MapID == 0 || currMapID == goalPt.MapID;
                        bool inCircle = onCorrectMap && IsWithinRange(currPos, goalPt);

                        // Detect whether goal and start share the same zone (single-arena).
                        bool sameArea = startCp &&
                            startCp->Point.TriggerType == ETriggerType::CombatArena &&
                            startCp->Point.MapID  == goalPt.MapID  &&
                            startCp->Point.X      == goalPt.X      &&
                            startCp->Point.Y      == goalPt.Y      &&
                            startCp->Point.Z      == goalPt.Z      &&
                            startCp->Point.Radius == goalPt.Radius;

                        if (!CombatGoal.finished)
                        {
                            if (!CombatGoal.active)
                            {
                                // Only arm independently if this is a different zone from start.
                                // Same-zone goal trackers are armed alongside the start (see above).
                                if (!sameArea)
                                {
                                    bool risingEdge = currInCombat && !prevInCombat;
                                    if (risingEdge && onCorrectMap && inCircle)
                                    {
                                        CombatGoal.active = true;
                                        CombatGoal.state  = ECombatState::Armed;
                                        char splitStartName[68];
                                        snprintf(splitStartName, sizeof(splitStartName), "%s Combat Start", goalCp->Name);
                                        SpeedrunTimer.AddSplit(splitStartName);
                                    }
                                }
                            }
                            else
                            {
                                goalTriggered = TickCombat(CombatGoal, goalPt, onCorrectMap, inCircle);
                                if (goalTriggered)
                                    goalTime = CombatGoal.dropTime > 0.0
                                        ? CombatGoal.dropTime
                                        : SpeedrunTimer.GetElapsedSeconds();
                            }
                        }
                    }
                    // All other goal types (Circle, Plane, MapChange, CircleInteract).
                    else
                    {
                        bool goalOnCorrectMap = goalPt.MapID == 0 ||
                            (goalPt.TriggerType == ETriggerType::MapChange
                            ? true
                            : currMapID == goalPt.MapID);
                        goalTriggered = goalOnCorrectMap &&
                            PointTriggered(prevPos, currPos, prevMapID, currMapID, goalPt);
                    }

                    // --- Run finished ---
                    if (goalTriggered)
                    {
                        // For CombatArena goals we have a back-dated stop time; use it so
                        // the final split time reflects when combat actually ended.
                        if (goalPt.TriggerType == ETriggerType::CombatArena && goalTime > 0.0)
                            SpeedrunTimer.StopAt(goalTime);
                        else
                            SpeedrunTimer.Stop();
                        GrandTimer.Stop();
                        RunFinished = true;

                        HistoricalRun run;
                        run.Date       = GetCurrentDateTimeString();
                        run.TotalTime  = SpeedrunTimer.GetElapsedSeconds();
                        // Use the pre-load-screen snapshot for grand total if available,
                        // otherwise use the live GrandTimer value.
                        run.GrandTotal = (pendingGrandStop >= 0.0)
                            ? pendingGrandStop
                            : GrandTimer.GetElapsedSeconds();
                        DisplayedGrandTotal = run.GrandTotal;
                        pendingGrandStop    = -1.0;
                        run.Splits          = SpeedrunTimer.GetSplits();

                        // Ensure the goal checkpoint itself appears as the final split entry
                        // even if the goal trigger type doesn't naturally produce one.
                        if (run.Splits.empty() || strcmp(run.Splits.back().Name, goalCp->Name) != 0)
                        {
                            Split goalSplit;
                            strncpy(goalSplit.Name, goalCp->Name, sizeof(goalSplit.Name) - 1);
                            goalSplit.Timestamp = run.TotalTime;
                            run.Splits.push_back(goalSplit);
                        }

                        HistoryRuns.insert(HistoryRuns.begin(), run);
                        if (BestRunIndex >= 0)
                            BestRunIndex++;
                        if ((int)HistoryRuns.size() > MaxHistoryRuns)
                            HistoryRuns.resize(MaxHistoryRuns);

                        if (!CurrentHistoryPath.empty())
                            SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, BestRunIndex);
                    }
                }
            }

            // MapChange start: deferred until the load screen has fully cleared so the
            // timer starts the moment the player can actually move, not during the black screen.
            // NOTE: It may look like PendingStart and the goal trigger could fire in the same
            // frame, instantly finishing a run the moment it starts. This cannot happen:
            // The goal block is guarded by SpeedrunTimer.IsRunning(), and PendingStart only
            // starts the timer at the end of the frame — so the goal check is always at least
            // one frame behind the start. MapChange start+goal on the same point is also safe:
            // by the time PendingStart fires, prevMapID has already moved on, so the MapChange
            // condition (prevMapID == point.MapID) is false and the goal does not trigger.
            if (PendingStart && !isLoading)
            {
                FullReset();
                SpeedrunTimer.Start();
                GrandTimer.Start();
            }

            // Update grand total display value for this frame.
            // Run in progress — keep the overlay ticking (frozen if a pending snapshot exists).
            if (SpeedrunTimer.IsRunning())
                DisplayedGrandTotal = (pendingGrandStop >= 0.0) ? pendingGrandStop : GrandTimer.GetElapsedSeconds();
        
        }
    } // KeybindMutex released here

    // Debug: log map transitions to the Nexus log panel so route authors can
    // identify the correct MapID values when building routes.
    if (ShowDebug && !isLoading && currMapID != prevMapID)
    {
        char buf[128];
        unsigned int startMapID = startCp ? startCp->Point.MapID : 0;
        snprintf(buf, sizeof(buf), "Map changed: %u -> %u (startMapID: %u)",
            prevMapID, currMapID, startMapID);
        APIDefs->Log(LOGL_DEBUG, "Split Wars 2", buf);
    }

    // --- Advance per-frame tracking state for next frame ---
    wasLoading   = isLoading;
    prevPos      = currPos;
    // Only update prevMapID when not loading — this keeps the MapChange trigger
    // stable across load screens (prevMapID still reflects the map we came from).
    if (!isLoading)
        prevMapID = currMapID;
    prevInCombat       = currInCombat;
    InteractKeyPressed = false; // Consumed — clear for next frame

    // --- Draw overlays ---
    RenderZones();
    RenderTimerOverlay();
    RenderConfigWindow();
    RenderHistoryWindow();
    RenderRouteBrowserWindow();
    RenderDebugWindow();
}

// ---------------------------------------------------------------------------
// AddonOptions
// ---------------------------------------------------------------------------
// Draws the Split Wars 2 section inside the Nexus options panel.
// All the standard ImGui widgets write directly into the global booleans and
// enums; the "Save Settings" button at the bottom persists them to disk.
// ---------------------------------------------------------------------------
void AddonOptions()
{
    //Timer related UI
    ImGui::Checkbox("Show Timer",         &ShowTimer);
    Tooltip("Toggles the speedrun timer overlay.");
    ImGui::SameLine();
    // Cycle button — label reflects the current mode so the player always
    // knows what clicking it will do.
    const char* timerModeLabel = (TimerDisplayMode == TimerMode::Segment)  ? "Mode: Segment"
                               : (TimerDisplayMode == TimerMode::LiveSplit) ? "Mode: LiveSplit"
                               :                                               "Mode: Split";
    if (ImGui::Button(timerModeLabel))
        TimerDisplayMode = (TimerMode)(((int)TimerDisplayMode + 1) % 3);
    Tooltip("Controls how split times and differences are displayed.\n\n"
            "Segment   - Each row shows the time for that segment only.\n"
            "            Diffs compare against your best time for that segment.\n\n"
            "Split     - Each row shows the elapsed time since the run started.\n"
            "            Diffs show how far ahead or behind you are overall.\n\n"
            "LiveSplit - Each row shows the time for that segment only.\n"
            "            Diffs still show your overall lead or deficit,\n"
            "            matching the behaviour of LiveSplit.");
    ImGui::Checkbox("Show Grand Total",   &ShowGrandTotal);
    Tooltip("Adds an additional timer to the split timer.\nThis will show the time including the load screens.");
    ImGui::SameLine();
    ImGui::Checkbox("Compact Mode",       &CompactMode);
    Tooltip("Reduces the timer to one line.");
    ImGui::Separator();

    //Route related UI
    ImGui::Checkbox("Show Route Config", &ShowConfig);
    Tooltip("Toggles the route configuration window.");
    ImGui::SameLine();
    ImGui::Checkbox("Show Route Browser", &ShowRouteBrowser);
    Tooltip("Toggles the route file browser.");
    ImGui::Checkbox("Show History",       &ShowHistory);
    Tooltip("Toggles the history window.");
    ImGui::SameLine();
    ImGui::Dummy(ImVec2(2.0f, ImGui::GetFrameHeight()));
    ImGui::SameLine();
    // Max History Runs — clamped to [1, 100].
    ImGui::Text("Max");
    Tooltip("Set an amount between 1 and 100.");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragInt("##maxruns", &MaxHistoryRuns, 1.0f, 1, 100);
    ImGui::Separator();

    //Checkpoint/Zone related UI
    ImGui::Checkbox("Show Checkpoints",   &ShowZones);
    Tooltip("Toggles the visibility of checkpoints.");
    ImGui::SameLine();
    if (!ShowZones)
    {
        ImGui::PushItemFlag(ImGuiItemFlags_Disabled, true);
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, ImGui::GetStyle().Alpha * 0.5f);
    }
    float prevStart = ZoneFadeStart;
    float prevEnd   = ZoneFadeEnd;
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("##fadestart", &ZoneFadeStart, 1.0f, 0.0f, 0.0f, "%.0fm");
    Tooltip("Distance at which zones start fading out (metres)");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(80.0f);
    ImGui::DragFloat("##fadeend", &ZoneFadeEnd, 1.0f, 0.0f, 0.0f, "%.0fm");
    Tooltip("Distance at which zones are fully hidden (metres)");
    if (!ShowZones)
    {
        ImGui::PopItemFlag();
        ImGui::PopStyleVar();
    }
    // absolute bounds
    ZoneFadeStart = std::clamp(ZoneFadeStart, 1.0f, 1000.0f);
    ZoneFadeEnd   = std::clamp(ZoneFadeEnd,   1.0f, 1000.0f);
    
    // relationship
    if (ZoneFadeStart != prevStart && ZoneFadeStart >= ZoneFadeEnd)
        ZoneFadeEnd = ZoneFadeStart + 1.0f;
    if (ZoneFadeEnd != prevEnd && ZoneFadeEnd <= ZoneFadeStart)
        ZoneFadeStart = ZoneFadeEnd - 1.0f;
    ImGui::Separator();
    
    // Data source selector — lets the user choose between RTAPI and Mumble.
    ImGui::Text("Data Source:");
    ImGui::SameLine();
    const char* sourceLabel = (PreferredSource == EDataSource::RTAPI)   ? "RTAPI"
                            : (PreferredSource == EDataSource::Mumble)  ? "Mumble"
                            :                                              "Default";
    ImGui::SetNextItemWidth(80.0f);
    if (ImGui::BeginCombo("##datasource", sourceLabel))
    {
        if (ImGui::Selectable("Default", PreferredSource == EDataSource::Default))
            PreferredSource = EDataSource::Default;
        Tooltip("Use RTAPI if available, otherwise Mumble.");
        if (ImGui::Selectable("Mumble",  PreferredSource == EDataSource::Mumble))
            PreferredSource = EDataSource::Mumble;
        Tooltip("Always use Mumble, even if RTAPI is available.");
        if (ImGui::Selectable("RTAPI",   PreferredSource == EDataSource::RTAPI))
            PreferredSource = EDataSource::RTAPI;
        Tooltip("Always use RTAPI. Falls back to Mumble if RTAPI is unavailable.");
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(GS.RTAPIAvailable ? "(RTAPI connected)" : "(RTAPI not available)");

    // Debug
    ImGui::Separator();
    ImGui::Checkbox("Show Debug", &ShowDebug);
    Tooltip("Toggles debugging text which is not fully implemented");

    //Save Settings
    if (ImGui::Button("Save Settings"))
        SaveSettings(AddonDir, GatherSettings());
}