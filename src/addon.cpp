#include "renderer_shared.h"
#include "worldrender.h"
#include "hotbar_icon.h"

AddonDefinition_t AddonDef{};

void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();
void HandleIdentityUpdate(void* aEventArgs);

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    AddonDef.Signature          = 0x53573200;
    AddonDef.APIVersion         = NEXUS_API_VERSION;
    AddonDef.Name               = "Split Wars 2";
    AddonDef.Version.Major      = 0;
    AddonDef.Version.Minor      = 15;
    AddonDef.Version.Build      = 6;
    AddonDef.Version.Revision   = 2;
    AddonDef.Author             = "Xenophy.2716";
    AddonDef.Description        = "A speedrun timer with coordinate-based triggers.";
    AddonDef.Load               = AddonLoad;
    AddonDef.Unload             = AddonUnload;
    AddonDef.Flags              = AF_None;
    AddonDef.Provider           = UP_GitHub;
    AddonDef.UpdateLink         = "https://github.com/Xen0phy/Split-Wars-2";

    return &AddonDef;
}

static void OnInteractKey(const char* aIdentifier, bool aIsRelease)
{
    if (!aIsRelease)
        InteractKeyPressed = true;
}

static void OnStartStopKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    std::lock_guard<std::mutex> lock(KeybindMutex);
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
        HistoryRuns.insert(HistoryRuns.begin(), run);
        if ((int)HistoryRuns.size() > MaxHistoryRuns)
            HistoryRuns.resize(MaxHistoryRuns);
        if (!CurrentHistoryPath.empty())
            SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, -1);
    }
    else
    {
        FullReset();
        SpeedrunTimer.Start();
        GrandTimer.Start();
        SpeedrunTimer.AddSplit("Manual Start");
    }
}

static void OnCheckpointKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    std::lock_guard<std::mutex> lock(KeybindMutex);
    if (SpeedrunTimer.IsRunning())
    {
        SpeedrunTimer.AddSplit("Manual Checkpoint");
    }
    else if (MumbleLink)
    {
        Checkpoint cp;
        snprintf(cp.Name, sizeof(cp.Name), "Checkpoint %d", (int)CurrentRoute.Checkpoints.size() + 1);
        cp.Point.X     = MumbleLink->AvatarPosition.X;
        cp.Point.Y     = MumbleLink->AvatarPosition.Y;
        cp.Point.Z     = MumbleLink->AvatarPosition.Z;
        cp.Point.MapID = MumbleLink->Context.MapID;
        CurrentRoute.Checkpoints.push_back(cp);
    }
}

static void OnShowTimerKey          (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowTimer        = !ShowTimer;        }
static void OnShowConfigKey         (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowConfig       = !ShowConfig;       }
static void OnShowHistoryKey        (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowHistory      = !ShowHistory;      }
static void OnShowZonesKey          (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowZones        = !ShowZones;        }
static void OnCycleTimerModeKey     (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) TimerDisplayMode = (TimerMode)(((int)TimerDisplayMode + 1) % 3); }
static void OnCompactModeKey        (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) CompactMode      = !CompactMode;      }
static void OnShowRouteBrowserKey   (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowRouteBrowser = !ShowRouteBrowser; }

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

static void OnResetTimerKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    std::lock_guard<std::mutex> lock(KeybindMutex);
    FullReset();
}

static void ApplySettings(const Settings& s)
{
    ShowTimer        = s.ShowTimer;
    ShowConfig       = s.ShowConfig;
    ShowZones        = s.ShowZones;
    ShowDebug        = s.ShowDebug;
    TimerDisplayMode = (TimerMode)s.TimerDisplayMode;
    CompactMode      = s.CompactMode;
    ShowHistory      = s.ShowHistory;
    ShowGrandTotal   = s.ShowGrandTotal;
    ShowRouteBrowser = s.ShowRouteBrowser;
    MaxHistoryRuns   = s.MaxHistoryRuns;
}

static Settings GatherSettings()
{
    Settings s;
    s.ShowTimer        = ShowTimer;
    s.ShowConfig       = ShowConfig;
    s.ShowZones        = ShowZones;
    s.ShowDebug        = ShowDebug;
    s.TimerDisplayMode = (int)TimerDisplayMode;
    s.CompactMode      = CompactMode;
    s.ShowHistory      = ShowHistory;
    s.ShowGrandTotal   = ShowGrandTotal;
    s.ShowRouteBrowser = ShowRouteBrowser;
    s.MaxHistoryRuns   = MaxHistoryRuns;
    return s;
}

static void AddonQuickAccessMenu()
{
    bool anyVisible = ShowTimer || ShowConfig || ShowHistory || ShowZones || ShowRouteBrowser;

    if (ImGui::MenuItem("Timer",         nullptr, &ShowTimer))        {}
    if (ImGui::MenuItem("Route Config",  nullptr, &ShowConfig))       {}
    if (ImGui::MenuItem("History",       nullptr, &ShowHistory))      {}
    if (ImGui::MenuItem("Route Browser", nullptr, &ShowRouteBrowser)) {}
    if (ImGui::MenuItem("Checkpoints",   nullptr, &ShowZones))        {}
}

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

void AddonLoad(AddonAPI_t* aApi)
{
    APIDefs = aApi;

    ImGui::SetCurrentContext((ImGuiContext*)APIDefs->ImguiContext);
    ImGui::SetAllocatorFunctions(
        (void* (*)(size_t, void*))APIDefs->ImguiMalloc,
        (void(*)(void*, void*))APIDefs->ImguiFree
    );

    MumbleLink = (Mumble::Data*)APIDefs->DataLink_Get("DL_MUMBLE_LINK");
    AddonDir   = GetAddonDir();

    Settings s;
    if (LoadSettings(AddonDir, s))
        ApplySettings(s);

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);
    APIDefs->Events_Subscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityUpdate);

    // Load hotbar icon textures from embedded memory and register QuickAccess shortcut
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
    APIDefs->QuickAccess_Add(
        "QA_SW2_HIDE_TOGGLE",
        "TEX_SW2_HOTBAR",
        "TEX_SW2_HOTBAR_HOVER",
        "SW2 Toggle Hide All Windows",
        "Split Wars 2: Hide/Restore Windows"
    );
    APIDefs->QuickAccess_AddContextMenu("QA_SW2_CTXMENU", "QA_SW2_HIDE_TOGGLE", AddonQuickAccessMenu);

    //Register all the keybinds
    for (auto& kb : Keybinds)
    APIDefs->InputBinds_RegisterWithStruct(kb.ID, kb.Fn, Keybind_t{});

    //Info in Nexus log
    APIDefs->Log(LOGL_INFO, "Split Wars 2", "Split Wars 2 loaded.");
}

void AddonUnload()
{
    SaveSettings(AddonDir, GatherSettings());
    
    //Unregister all the keybinds
    for (auto& kb : Keybinds)
        APIDefs->InputBinds_Deregister(kb.ID);

    APIDefs->QuickAccess_Remove("QA_SW2_HIDE_TOGGLE");
    APIDefs->QuickAccess_RemoveContextMenu("QA_SW2_CTXMENU");
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->Events_Unsubscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityUpdate);
    APIDefs->Log(LOGL_INFO, "Split Wars 2", "Split Wars 2 unloaded.");
}

void HandleIdentityUpdate(void* aEventArgs)
{
    if (!aEventArgs) return;
    Mumble::Identity* identity = (Mumble::Identity*)aEventArgs;
    CameraFOV = identity->FOV;
}

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

// Called every frame by Nexus. Handles loading screen detection, all start/checkpoint/goal
// trigger logic, and dispatches to the individual render windows. Timer mutations are
// guarded by KeybindMutex so this function and the keybind callbacks can't race each other.
void AddonRender()
{
    if (!MumbleLink) return;

    // --- Per-frame state (persists between calls) ---
    static unsigned int lastUITick       = 0;
    static bool         wasLoading       = false;
    static Vector3      prevPos          = {0, 0, 0};
    static unsigned int prevMapID        = 0;
    static bool         prevInCombat     = false;
    static double       pendingGrandStop = -1.0;  // holds GrandTimer snapshot for MapChange goals

    // Snapshot current game state for this frame
    Vector3      currPos      = MumbleLink->AvatarPosition;
    unsigned int currMapID    = MumbleLink->Context.MapID;
    bool         currInCombat = MumbleLink->Context.IsInCombat;

    // -------------------------------------------------------------------------
    // CombatArena trigger helper
    // Advances a CombatTriggerState one frame and returns true when the
    // combat segment is considered finished (player left combat or the zone).
    // -------------------------------------------------------------------------
    auto TickCombat = [&](CombatTriggerState& cs, const RoutePoint& point,
                          bool onCorrectMap, bool inCircle) -> bool
    {
        constexpr double GraceDuration = 2.0;

        if (cs.finished) return false;

        // Not yet active — wait for a rising combat edge inside the zone
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

        // Armed: player is in combat inside the zone
        if (cs.state == ECombatState::Armed)
        {
            // Left the zone while still in combat — trigger immediately
            if (!inCircle)
            {
                cs.active   = false;
                cs.finished = true;
                return true;
            }
            // Dropped combat while still in zone — start grace period
            if (!currInCombat)
            {
                cs.state      = ECombatState::GracePending;
                cs.dropTime   = SpeedrunTimer.GetElapsedSeconds();
                cs.graceStart = std::chrono::steady_clock::now();
            }
            return false;
        }

        // GracePending: combat dropped, waiting to see if it comes back
        else if (cs.state == ECombatState::GracePending)
        {
            // Still out of combat and inside the zone — check if grace period expired
            if (!currInCombat && inCircle)
            {
                auto elapsed = std::chrono::duration<double>(
                    std::chrono::steady_clock::now() - cs.graceStart).count();
                if (elapsed >= GraceDuration)
                {
                    // Grace period expired — combat segment is done
                    cs.active   = false;
                    cs.finished = true;
                    return true;
                }
                return false;
            }

            // Re-entered combat before grace expired — go back to Armed
            if (currInCombat && inCircle)
            {
                cs.state    = ECombatState::Armed;
                cs.dropTime = 0.0;
                return false;
            }

            // Left the zone during grace period — trigger
            cs.active   = false;
            cs.finished = true;
            return true;
        }

        return false;
    };

    // -------------------------------------------------------------------------
    // Loading screen detection
    // UITick freezes while a load screen is active.
    // -------------------------------------------------------------------------
    bool isLoading = (MumbleLink->UITick == lastUITick);
    lastUITick     = MumbleLink->UITick;

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

            // For MapChange goals: snapshot the GrandTimer now (before the map
            // actually changes) so load screen time is excluded from grand total.
            if (SpeedrunTimer.IsRunning() && goalCp &&
                goalCp->Point.TriggerType == ETriggerType::MapChange &&
                goalCp->Point.MapID != 0 &&
                currMapID == goalCp->Point.MapID)
            {
                pendingGrandStop = GrandTimer.GetElapsedSeconds();
            }
        }
        else if (!isLoading && wasLoading)
        {
            SpeedrunTimer.Resume();

            // If we didn't actually leave the goal map, discard the snapshot
            if (pendingGrandStop >= 0.0 && goalCp && currMapID == goalCp->Point.MapID)
                pendingGrandStop = -1.0;
        }

    if (CurrentRoute.IsValid)
    {
        // Keep the parallel trigger-state arrays in sync with the route length
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

            // Circle: reset when player enters, start when they leave
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
            // Plane: start when player crosses the trigger plane
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
            // Map Change: queue a start for after the load screen clears
            else if (startPt.TriggerType == ETriggerType::MapChange)
            {
                bool justLeft = (prevMapID == startPt.MapID) && (currMapID != startPt.MapID);
                if (startPt.MapID != 0 && justLeft &&
                    !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
                {
                    PendingStart = true;
                }
            }
            // Circle Interact: start when player presses interact inside the zone
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
            // Combat Arena: start on rising combat edge inside the zone
            else if (startPt.TriggerType == ETriggerType::CombatArena)
            {
                bool onCorrectMap = startPt.MapID == 0 || currMapID == startPt.MapID;
                bool inCircle = onCorrectMap && IsWithinRange(currPos, startPt);

                // Auto-rearm once a previous combat segment fully resolved
                if (CombatStart.finished && !SpeedrunTimer.IsRunning())
                    CombatStart = {};

                // True when start and goal share the exact same zone (single-arena run)
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

                        // For single-arena runs arm the goal tracker at the same time
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
                    // and record a split when it finishes
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
                // Start and goal are handled separately
                if (CurrentRoute.Checkpoints[i].IsStart || CurrentRoute.Checkpoints[i].IsGoal)
                    continue;

                const RoutePoint& cp = CurrentRoute.Checkpoints[i].Point;

                bool onCorrectMap = cp.TriggerType == ETriggerType::MapChange ||
                                    cp.MapID == 0 ||
                                    currMapID == cp.MapID;

                bool triggered = false;
                if (onCorrectMap)
                {
                    if (cp.TriggerType == ETriggerType::CombatArena)
                    {
                        // Combat checkpoints record a "Combat Start" split on
                        // entry and a "Combat End" split when the segment finishes
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

                // Record the split — Circle triggers require the player to have
                // been outside the zone first to prevent re-triggering on entry
                if (triggered && !checkpointTriggered[i] &&
                    (cp.TriggerType != ETriggerType::Circle || !WasInCheckpoint[i]))
                {
                    if (cp.TriggerType == ETriggerType::CombatArena &&
                        CombatCheckpoints[i].dropTime > 0.0)
                    {
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

                // Track whether the player was inside a circle zone last frame
                // (used to enforce the exit-before-retrigger rule above)
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

                // All Checkpoints: fires once every non-start/non-goal checkpoint is hit
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
                // Combat Arena goal: tick the combat tracker, same logic as checkpoints
                else if (goalPt.TriggerType == ETriggerType::CombatArena)
                {
                    bool onCorrectMap = goalPt.MapID == 0 || currMapID == goalPt.MapID;
                    bool inCircle = onCorrectMap && IsWithinRange(currPos, goalPt);

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
                            // Only arm independently if goal is a different zone to start
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
                // All other goal types (Circle, Plane, MapChange, CircleInteract)
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
                    if (goalPt.TriggerType == ETriggerType::CombatArena && goalTime > 0.0)
                        SpeedrunTimer.StopAt(goalTime);
                    else
                        SpeedrunTimer.Stop();
                    GrandTimer.Stop();
                    RunFinished = true;

                    HistoricalRun run;
                    run.Date       = GetCurrentDateTimeString();
                    run.TotalTime  = SpeedrunTimer.GetElapsedSeconds();
                    run.GrandTotal = (pendingGrandStop >= 0.0)
                        ? pendingGrandStop
                        : GrandTimer.GetElapsedSeconds();
                    DisplayedGrandTotal = run.GrandTotal;
                    pendingGrandStop    = -1.0;
                    run.Splits          = SpeedrunTimer.GetSplits();

                    // Ensure the goal checkpoint itself appears as the final split
                    if (run.Splits.empty() || strcmp(run.Splits.back().Name, goalCp->Name) != 0)
                    {
                        Split goalSplit;
                        strncpy(goalSplit.Name, goalCp->Name, sizeof(goalSplit.Name) - 1);
                        goalSplit.Timestamp = run.TotalTime;
                        run.Splits.push_back(goalSplit);
                    }

                    HistoryRuns.insert(HistoryRuns.begin(), run);
                    if ((int)HistoryRuns.size() > MaxHistoryRuns)
                        HistoryRuns.resize(MaxHistoryRuns);

                    if (!CurrentHistoryPath.empty())
                        SaveHistory(CurrentHistoryPath, BestRun, HistoryRuns, -1);
                }
            }
        }

        // MapChange start: deferred until the load screen has cleared
        if (PendingStart && !isLoading)
        {
            FullReset();
            SpeedrunTimer.Start();
            GrandTimer.Start();
        }
    }
    } // KeybindMutex

    // -------------------------------------------------------------------------
    // Update the grand total overlay value outside the lock — read-only here.
    // -------------------------------------------------------------------------

    // Run was manually reset or never started — clear overlay
    if (!SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
    {
        pendingGrandStop    = -1.0;
        DisplayedGrandTotal = 0.0;
    }
    // Run in progress — keep the overlay ticking (frozen if pending snapshot exists)
    else if (SpeedrunTimer.IsRunning())
        DisplayedGrandTotal = (pendingGrandStop >= 0.0) ? pendingGrandStop : GrandTimer.GetElapsedSeconds();

    // Debug: log map transitions to the Nexus log panel
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
    if (!isLoading)
        prevMapID = currMapID;
    prevInCombat       = currInCombat;
    InteractKeyPressed = false;

    // --- Draw overlays ---
    RenderZones();
    RenderTimerOverlay();
    RenderConfigWindow();
    RenderHistoryWindow();
    RenderRouteBrowserWindow();
}

void AddonOptions()
{
    ImGui::Checkbox("Show Timer",         &ShowTimer);
    Tooltip("Toggles the speedrun timer overlay.");
    ImGui::Checkbox("Show Route Config", &ShowConfig);
    Tooltip("Toggles the route configuration window.");
    ImGui::Checkbox("Show History",       &ShowHistory);
    Tooltip("Toggles the history window.");
    ImGui::Checkbox("Show Route Browser", &ShowRouteBrowser);
    Tooltip("Toggles the route file browser.");
    ImGui::Checkbox("Show Checkpoints",   &ShowZones);
    Tooltip("Toggles the visibility of checkpoints.");
    ImGui::Checkbox("Show Debug", &ShowDebug);
    Tooltip("Toggles debugging text which is not fully implemented");
    ImGui::Separator();
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
    ImGui::Checkbox("Compact Mode",       &CompactMode);
    Tooltip("Reduces the timer to one line.");
    ImGui::Checkbox("Show Grand Total",   &ShowGrandTotal);
    Tooltip("Adds an additional timer to the split timer.\nThis will show the time including the load screens.");
    ImGui::Separator();
    ImGui::Text("Max History Runs");
    Tooltip("Set an amount between 1 and 100.");
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("##maxruns", &MaxHistoryRuns, 0, 0);
    if (MaxHistoryRuns < 1)   MaxHistoryRuns = 1;
    if (MaxHistoryRuns > 100) MaxHistoryRuns = 100;
    ImGui::Separator();
    if (ImGui::Button("Save Settings"))
        SaveSettings(AddonDir, GatherSettings());
}
