#include "shared.h"
#include "renderer.h"
#include "worldrender.h"
#include "hotbar_icon.h"

AddonDefinition_t AddonDef{};

void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();
void HandleIdentityUpdate(void* aEventArgs);
static void OnHotbarToggleKey(const char* aIdentifier, bool aIsRelease);

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
            SaveHistory(CurrentHistoryPath, BestSplits, HistoryRuns);
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

static void OnShowTimerKey         (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowTimer        = !ShowTimer;        }
static void OnShowConfigKey        (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowConfig       = !ShowConfig;       }
static void OnShowHistoryKey       (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowHistory      = !ShowHistory;      }
static void OnShowZonesKey         (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowZones        = !ShowZones;        }
static void OnCycleTimerModeKey        (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) TimerDisplayMode = (TimerMode)(((int)TimerDisplayMode + 1) % 3); }
static void OnCompactModeKey       (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) CompactMode      = !CompactMode;      }
static void OnShowRouteBrowserKey  (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowRouteBrowser = !ShowRouteBrowser; }

static void OnHotbarToggleKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    if (!HotbarWindowsHidden)
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
        // Restore saved visibility
        ShowTimer        = HotbarSavedShowTimer;
        ShowConfig       = HotbarSavedShowConfig;
        ShowHistory      = HotbarSavedShowHistory;
        ShowZones        = HotbarSavedShowZones;
        ShowRouteBrowser = HotbarSavedShowRouteBrowser;
        HotbarWindowsHidden = false;
    }
}

static void OnResetTimerKey(const char* aIdentifier, bool aIsRelease)
{
    if (aIsRelease) return;
    std::lock_guard<std::mutex> lock(KeybindMutex);
    FullReset();
}

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    AddonDef.Signature        = 0x53573200;
    AddonDef.APIVersion       = NEXUS_API_VERSION;
    AddonDef.Name             = "Split Wars 2";
    AddonDef.Version.Major    = 0;
    AddonDef.Version.Minor    = 14;
    AddonDef.Version.Build    = 5;
    AddonDef.Version.Revision = 0;
    AddonDef.Author           = "Xenophy";
    AddonDef.Description      = "A speedrun timer with coordinate-based triggers.";
    AddonDef.Load             = AddonLoad;
    AddonDef.Unload           = AddonUnload;
    AddonDef.Flags            = AF_None;
    AddonDef.Provider         = UP_None;
    AddonDef.UpdateLink       = nullptr;

    return &AddonDef;
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
    APIDefs->InputBinds_RegisterWithStruct("SW2 Show Timer",           OnShowTimerKey,          Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Interact",             OnInteractKey,           Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Start/Stop",           OnStartStopKey,          Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Reset Timer",          OnResetTimerKey,         Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Show Route Browser",   OnShowRouteBrowserKey,   Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Show Route Config",    OnShowConfigKey,         Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Show Route History",   OnShowHistoryKey,        Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Cycle Timer Mode",     OnCycleTimerModeKey,     Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Toggle Compact Mode",  OnCompactModeKey,        Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Add/Call Checkpoint",  OnCheckpointKey,         Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("SW2 Show Zones",           OnShowZonesKey,          Keybind_t{});

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
    APIDefs->InputBinds_RegisterWithStruct("SW2 Toggle Hide All Windows", OnHotbarToggleKey, Keybind_t{});
}

void AddonUnload()
{
    SaveSettings(AddonDir, GatherSettings());
    
    APIDefs->InputBinds_Deregister("SW2 Show Timer");
    APIDefs->InputBinds_Deregister("SW2 Interact");
    APIDefs->InputBinds_Deregister("SW2 Start/Stop");
    APIDefs->InputBinds_Deregister("SW2 Reset Timer");
    APIDefs->InputBinds_Deregister("SW2 Show Route Browser");
    APIDefs->InputBinds_Deregister("SW2 Show Route Config");
    APIDefs->InputBinds_Deregister("SW2 Show Route History");
    APIDefs->InputBinds_Deregister("SW2 Cycle Timer Mode");
    APIDefs->InputBinds_Deregister("SW2 Toggle Compact Mode");
    APIDefs->InputBinds_Deregister("SW2 Add/Call Checkpoint");
    APIDefs->InputBinds_Deregister("SW2 Show Zones");
    APIDefs->QuickAccess_Remove("QA_SW2_HIDE_TOGGLE");
    APIDefs->InputBinds_Deregister("SW2 Toggle Hide All Windows");
    APIDefs->GUI_Deregister(AddonRender);
    APIDefs->GUI_Deregister(AddonOptions);
    APIDefs->Events_Unsubscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityUpdate);
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

void AddonRender()
{
    if (!MumbleLink) return;

    static unsigned int lastUITick       = 0;
    static bool         wasLoading       = false;
    static Vector3      prevPos          = {0, 0, 0};
    static unsigned int prevMapID        = 0;
    static bool         prevInCombat     = false;
    static double       pendingGrandStop = -1.0;  // buffered GrandTimer value for MapChange goal

    Vector3      currPos      = MumbleLink->AvatarPosition;
    unsigned int currMapID    = MumbleLink->Context.MapID;
    bool         currInCombat = MumbleLink->Context.IsInCombat;

    auto TickCombat = [&](CombatTriggerState& cs, const RoutePoint& point,
                          bool onCorrectMap, bool inCircle) -> bool
    {
        constexpr double GraceDuration = 2.0;

        if (cs.finished) return false;

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

        if (cs.state == ECombatState::Armed)
        {
            if (!inCircle)
            {
                cs.active   = false;
                cs.finished = true;
                return true;
            }
            if (!currInCombat)
            {
                cs.state     = ECombatState::GracePending;
                cs.dropTime  = SpeedrunTimer.GetElapsedSeconds();
                cs.graceStart = std::chrono::steady_clock::now();
            }
            return false;
        }

        if (!currInCombat && inCircle)
        {
            auto elapsed = std::chrono::duration<double>(
                std::chrono::steady_clock::now() - cs.graceStart).count();
            if (elapsed >= GraceDuration)
            {
                cs.active   = false;
                cs.finished = true;
                return true;
            }
            return false;
        }

        if (currInCombat && inCircle)
        {
            cs.state    = ECombatState::Armed;
            cs.dropTime = 0.0;
            return false;
        }

        cs.active   = false;
        cs.finished = true;
        return true;
    };

    // Loading screen detection
    bool isLoading = (MumbleLink->UITick == lastUITick);
    lastUITick     = MumbleLink->UITick;

    if (isLoading && !wasLoading)
    {
        SpeedrunTimer.Pause();
        // If goal is MapChange and we're currently on the goal map, buffer the
        // GrandTimer now (before the load screen) so the grand total excludes it.
        if (SpeedrunTimer.IsRunning() &&
            CurrentRoute.Goal.TriggerType == ETriggerType::MapChange &&
            CurrentRoute.Goal.MapID != 0 &&
            currMapID == CurrentRoute.Goal.MapID)
        {
            pendingGrandStop = GrandTimer.GetElapsedSeconds();
        }
    }
    else if (!isLoading && wasLoading)
    {
        SpeedrunTimer.Resume();
        // Discard the buffer if we didn't actually leave the goal map.
        if (pendingGrandStop >= 0.0 && currMapID == CurrentRoute.Goal.MapID)
            pendingGrandStop = -1.0;
    }
    wasLoading = isLoading;

    // If the timer is no longer running (manual reset, etc.), discard any buffered grand stop.
    if (!SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
    {
        pendingGrandStop    = -1.0;
        DisplayedGrandTotal = 0.0;
    }
    // Keep the overlay value live while a run is in progress.
    else if (SpeedrunTimer.IsRunning())
        DisplayedGrandTotal = (pendingGrandStop >= 0.0) ? pendingGrandStop : GrandTimer.GetElapsedSeconds();

    {
    std::lock_guard<std::mutex> lock(KeybindMutex);
    if (CurrentRoute.IsValid)
    {
        if (WasInCheckpoint.size() != CurrentRoute.Checkpoints.size())
        {
            WasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
            checkpointTriggered.assign(CurrentRoute.Checkpoints.size(), false);
            CombatCheckpoints.assign(CurrentRoute.Checkpoints.size(), {});
        }

        // --- Circle start logic ---
        if (CurrentRoute.Start.TriggerType == ETriggerType::Circle)
        {
            bool onCorrectMap = CurrentRoute.Start.MapID == 0 ||
                                currMapID == CurrentRoute.Start.MapID;
            bool inStart = onCorrectMap && IsWithinRange(currPos, CurrentRoute.Start);

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

        // --- Plane start logic ---
        else if (CurrentRoute.Start.TriggerType == ETriggerType::Plane)
        {
            bool onCorrectMap = CurrentRoute.Start.MapID == 0 ||
                                currMapID == CurrentRoute.Start.MapID;
            bool crossed = onCorrectMap && HasCrossedPlane(prevPos, currPos, CurrentRoute.Start);

            if (crossed && !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
            {
                FullReset();
                SpeedrunTimer.Start();
                GrandTimer.Start();
            }
        }

        // --- Map Change start logic ---
        else if (CurrentRoute.Start.TriggerType == ETriggerType::MapChange)
        {
            bool justLeft = (prevMapID == CurrentRoute.Start.MapID) && (currMapID != CurrentRoute.Start.MapID);
            if (CurrentRoute.Start.MapID != 0 &&
                justLeft &&
                !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
            {
                PendingStart = true;
            }
        }

        // --- Circle Interact start logic ---
        else if (CurrentRoute.Start.TriggerType == ETriggerType::CircleInteract)
        {
            bool onCorrectMap = CurrentRoute.Start.MapID == 0 ||
                                currMapID == CurrentRoute.Start.MapID;
            bool inCircle = onCorrectMap && IsWithinRange(currPos, CurrentRoute.Start);

            if (inCircle && InteractKeyPressed && !SpeedrunTimer.IsRunning())
            {
                FullReset();
                SpeedrunTimer.Start();
                GrandTimer.Start();
            }
        }
        // --- Combat Arena start logic ---
        else if (CurrentRoute.Start.TriggerType == ETriggerType::CombatArena)
        {
            bool onCorrectMap = CurrentRoute.Start.MapID == 0 ||
                                currMapID == CurrentRoute.Start.MapID;
            bool inCircle = onCorrectMap && IsWithinRange(currPos, CurrentRoute.Start);

            if (CombatStart.finished && !SpeedrunTimer.IsRunning())
                CombatStart = {};

            bool sameArea = CurrentRoute.Goal.TriggerType == ETriggerType::CombatArena &&
                CurrentRoute.Goal.MapID  == CurrentRoute.Start.MapID  &&
                CurrentRoute.Goal.X      == CurrentRoute.Start.X      &&
                CurrentRoute.Goal.Y      == CurrentRoute.Start.Y      &&
                CurrentRoute.Goal.Z      == CurrentRoute.Start.Z      &&
                CurrentRoute.Goal.Radius == CurrentRoute.Start.Radius;

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

                    if (sameArea)
                    {
                        CombatGoal.active = true;
                        CombatGoal.state  = ECombatState::Armed;
                    }
                }
            }
            else if (SpeedrunTimer.IsRunning() && CombatStart.active)
            {
                if (!sameArea)
                {
                    bool finished = TickCombat(CombatStart, CurrentRoute.Start, onCorrectMap, inCircle);
                    if (finished)
                    {
                        double t = CombatStart.dropTime > 0.0
                            ? CombatStart.dropTime
                            : SpeedrunTimer.GetElapsedSeconds();
                        Split s;
                        strncpy(s.Name, "Start Combat End", sizeof(s.Name) - 1);
                        s.Timestamp = t;
                        SpeedrunTimer.AddSplitAt(s);
                    }
                }
            }
        }

        // --- Checkpoint and goal logic (skip during loading) ---
        if (!isLoading && SpeedrunTimer.IsRunning())
        {
            for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
            {
                const RoutePoint& cp = CurrentRoute.Checkpoints[i].Point;

                bool onCorrectMap = cp.TriggerType == ETriggerType::MapChange ||
                                    cp.MapID == 0 ||
                                    currMapID == cp.MapID;

                bool triggered = false;
                if (onCorrectMap)
                {
                    if (cp.TriggerType == ETriggerType::CombatArena)
                    {
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

                WasInCheckpoint[i] = (cp.TriggerType == ETriggerType::Circle && onCorrectMap)
                    ? IsWithinRange(currPos, cp)
                    : false;
            }

            // Goal trigger check
            bool goalTriggered = false;
            double goalTime    = 0.0;
            if (CurrentRoute.Goal.TriggerType == ETriggerType::AllCheckpoints)
            {
                bool allDone = !CurrentRoute.Checkpoints.empty();
                for (int i = 0; i < (int)checkpointTriggered.size(); i++)
                    if (!checkpointTriggered[i]) { allDone = false; break; }
                goalTriggered = allDone;
            }
            else if (CurrentRoute.Goal.TriggerType == ETriggerType::CombatArena)
            {
                bool onCorrectMap = CurrentRoute.Goal.MapID == 0 ||
                                    currMapID == CurrentRoute.Goal.MapID;
                bool inCircle = onCorrectMap && IsWithinRange(currPos, CurrentRoute.Goal);

                bool sameArea = CurrentRoute.Start.TriggerType == ETriggerType::CombatArena &&
                    CurrentRoute.Start.MapID  == CurrentRoute.Goal.MapID  &&
                    CurrentRoute.Start.X      == CurrentRoute.Goal.X      &&
                    CurrentRoute.Start.Y      == CurrentRoute.Goal.Y      &&
                    CurrentRoute.Start.Z      == CurrentRoute.Goal.Z      &&
                    CurrentRoute.Start.Radius == CurrentRoute.Goal.Radius;

                if (!CombatGoal.finished)
                {
                    if (!CombatGoal.active)
                    {
                        if (!sameArea)
                        {
                            bool risingEdge = currInCombat && !prevInCombat;
                            if (risingEdge && onCorrectMap && inCircle)
                            {
                                CombatGoal.active = true;
                                CombatGoal.state  = ECombatState::Armed;
                                SpeedrunTimer.AddSplit("Goal Combat Start");
                            }
                        }
                    }
                    else
                    {
                        goalTriggered = TickCombat(CombatGoal, CurrentRoute.Goal, onCorrectMap, inCircle);
                        if (goalTriggered)
                            goalTime = CombatGoal.dropTime > 0.0
                                ? CombatGoal.dropTime
                                : SpeedrunTimer.GetElapsedSeconds();
                    }
                }
            }
            else
            {
                bool goalOnCorrectMap = CurrentRoute.Goal.MapID == 0 ||
                    (CurrentRoute.Goal.TriggerType == ETriggerType::MapChange
                    ? true
                    : currMapID == CurrentRoute.Goal.MapID);
                goalTriggered = goalOnCorrectMap &&
                    PointTriggered(prevPos, currPos, prevMapID, currMapID, CurrentRoute.Goal);
            }

            if (goalTriggered)
            {
                if (CurrentRoute.Goal.TriggerType == ETriggerType::CombatArena && goalTime > 0.0)
                    SpeedrunTimer.StopAt(goalTime);
                else
                    SpeedrunTimer.Stop();
                GrandTimer.Stop();
                RunFinished = true;

                HistoricalRun run;
                run.Date       = GetCurrentDateTimeString();
                run.TotalTime  = SpeedrunTimer.GetElapsedSeconds();
                // For MapChange goals, use the pre-load-screen GrandTimer value if available,
                // so the grand total excludes the load screen.
                run.GrandTotal = (pendingGrandStop >= 0.0)
                    ? pendingGrandStop
                    : GrandTimer.GetElapsedSeconds();
                DisplayedGrandTotal = run.GrandTotal;
                pendingGrandStop = -1.0;
                run.Splits     = SpeedrunTimer.GetSplits();

                if (run.Splits.empty() || strcmp(run.Splits.back().Name, "Goal") != 0)
                {
                    Split goalSplit;
                    strncpy(goalSplit.Name, "Goal", sizeof(goalSplit.Name) - 1);
                    goalSplit.Timestamp = run.TotalTime;
                    run.Splits.push_back(goalSplit);
                }

                HistoryRuns.insert(HistoryRuns.begin(), run);
                if ((int)HistoryRuns.size() > MaxHistoryRuns)
                    HistoryRuns.resize(MaxHistoryRuns);

                if (!CurrentHistoryPath.empty())
                    SaveHistory(CurrentHistoryPath, BestSplits, HistoryRuns);
            }
        }
    }
    } // KeybindMutex

    if (PendingStart && !isLoading)
    {
        FullReset();
        SpeedrunTimer.Start();
        GrandTimer.Start();
    }

    prevPos = currPos;
    if (!isLoading)
        prevMapID = currMapID;
    prevInCombat = currInCombat;

    InteractKeyPressed = false;

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
    ImGui::Checkbox("Show Debug Overlay", &ShowDebug);
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
