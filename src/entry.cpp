#include "shared.h"
#include "imgui.h"
#include "renderer.h"
#include "worldrender.h"

AddonDefinition_t AddonDef{};

void AddonLoad(AddonAPI_t* aApi);
void AddonUnload();
void AddonRender();
void AddonOptions();
void HandleIdentityUpdate(void* aEventArgs);

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
        SaveHistory(AddonDir, CurrentRouteName, BestSplits, HistoryRuns);
    }
    else
    {
        SpeedrunTimer.Reset();
        GrandTimer.Reset();
        RunFinished  = false;
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

static void OnShowTimerKey      (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowTimer      = !ShowTimer;      }
static void OnShowConfigKey     (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowConfig     = !ShowConfig;     }
static void OnShowHistoryKey    (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowHistory    = !ShowHistory;    }
static void OnShowZonesKey      (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) ShowZones      = !ShowZones;      }
static void OnSplitModeKey      (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) SplitMode      = !SplitMode;      }
static void OnCompactModeKey    (const char* aIdentifier, bool aIsRelease) { if (!aIsRelease) CompactMode    = !CompactMode;    }

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    AddonDef.Signature        = 0x53573200;
    AddonDef.APIVersion       = NEXUS_API_VERSION;
    AddonDef.Name             = "Split Wars 2";
    AddonDef.Version.Major    = 0;
    AddonDef.Version.Minor    = 11;
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
    ShowTimer      = s.ShowTimer;
    ShowConfig     = s.ShowConfig;
    ShowZones      = s.ShowZones;
    ShowDebug      = s.ShowDebug;
    SplitMode      = s.SplitMode;
    CompactMode    = s.CompactMode;
    ShowHistory    = s.ShowHistory;
    ShowGrandTotal = s.ShowGrandTotal;
    MaxHistoryRuns = s.MaxHistoryRuns;
}

static Settings GatherSettings()
{
    Settings s;
    s.ShowTimer      = ShowTimer;
    s.ShowConfig     = ShowConfig;
    s.ShowZones      = ShowZones;
    s.ShowDebug      = ShowDebug;
    s.SplitMode      = SplitMode;
    s.CompactMode    = CompactMode;
    s.ShowHistory    = ShowHistory;
    s.ShowGrandTotal = ShowGrandTotal;
    s.MaxHistoryRuns = MaxHistoryRuns;
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
    APIDefs->InputBinds_RegisterWithStruct("Interact", OnInteractKey, Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("Start/Stop",    OnStartStopKey,   Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("Add/Call Checkpoint",   OnCheckpointKey,  Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("Show Timer",   OnShowTimerKey,   Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("Show Config",  OnShowConfigKey,  Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("Show History", OnShowHistoryKey, Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("Show Zones",   OnShowZonesKey,   Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("Split Mode",   OnSplitModeKey,   Keybind_t{});
    APIDefs->InputBinds_RegisterWithStruct("Compact Mode", OnCompactModeKey, Keybind_t{});
}

void AddonUnload()
{
    SaveSettings(AddonDir, GatherSettings());

    APIDefs->InputBinds_Deregister("Interact");
    APIDefs->InputBinds_Deregister("Start/Stop");
    APIDefs->InputBinds_Deregister("Add/Call Checkpoint");
    APIDefs->InputBinds_Deregister("Show Timer");
    APIDefs->InputBinds_Deregister("Show Config");
    APIDefs->InputBinds_Deregister("Show History");
    APIDefs->InputBinds_Deregister("Show Zones");
    APIDefs->InputBinds_Deregister("Split Mode");
    APIDefs->InputBinds_Deregister("Compact Mode");
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
    static bool         wasInCircleStart = false;
    static std::vector<bool> wasInCheckpoint;
    static std::vector<bool> checkpointTriggered;

    Vector3      currPos   = MumbleLink->AvatarPosition;
    unsigned int currMapID = MumbleLink->Context.MapID;

    // Loading screen detection
    bool isLoading = (MumbleLink->UITick == lastUITick);
    lastUITick     = MumbleLink->UITick;

    if (isLoading && !wasLoading)
        SpeedrunTimer.Pause();
    else if (!isLoading && wasLoading)
    {
        SpeedrunTimer.Resume();
        if (PendingStart)
        {
            SpeedrunTimer.Reset();
            GrandTimer.Reset();
            SpeedrunTimer.Start();
            GrandTimer.Start();
            PendingStart = false;
            RunFinished  = false;
            wasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
            checkpointTriggered.assign(CurrentRoute.Checkpoints.size(), false);
        }
    }
    wasLoading = isLoading;

    {
    std::lock_guard<std::mutex> lock(KeybindMutex);
    if (CurrentRoute.IsValid)
    {
        if (wasInCheckpoint.size() != CurrentRoute.Checkpoints.size())
        {
            wasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
            checkpointTriggered.assign(CurrentRoute.Checkpoints.size(), false);  // 5
        }

        // --- Circle start logic ---
        if (CurrentRoute.Start.TriggerType == ETriggerType::Circle)
        {
            // Only trigger if on correct map (or MapID is 0)
            bool onCorrectMap = CurrentRoute.Start.MapID == 0 ||
                                currMapID == CurrentRoute.Start.MapID;
            bool inStart = onCorrectMap && IsWithinRange(currPos, CurrentRoute.Start);

            if (inStart && !wasInCircleStart && !SpeedrunTimer.IsRunning())
            {
                SpeedrunTimer.Reset();
                GrandTimer.Reset();
                RunFinished  = false;
                PendingStart = false;
                wasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
                checkpointTriggered.assign(CurrentRoute.Checkpoints.size(), false);
            }

            if (!inStart && wasInCircleStart &&
                !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
            {
                SpeedrunTimer.Start();
                GrandTimer.Start();
            }

            wasInCircleStart = inStart;
        }
        // --- Plane start logic ---
        else if (CurrentRoute.Start.TriggerType == ETriggerType::Plane)
        {
            bool onCorrectMap = CurrentRoute.Start.MapID == 0 ||
                                currMapID == CurrentRoute.Start.MapID;
            bool crossed = onCorrectMap && HasCrossedPlane(prevPos, currPos, CurrentRoute.Start);

            if (crossed && !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
            {
                SpeedrunTimer.Reset();
                GrandTimer.Reset();
                SpeedrunTimer.Start();
                GrandTimer.Start();
                RunFinished  = false;
                PendingStart = false;
                wasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
                checkpointTriggered.assign(CurrentRoute.Checkpoints.size(), false);
            }
        }
        // --- Map Change start logic ---
        else if (CurrentRoute.Start.TriggerType == ETriggerType::MapChange)
        {
            if (CurrentRoute.Start.MapID != 0 &&
                currMapID == CurrentRoute.Start.MapID &&
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
                SpeedrunTimer.Reset();
                GrandTimer.Reset();
                SpeedrunTimer.Start();
                GrandTimer.Start();
                RunFinished  = false;
                PendingStart = false;
                wasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
                checkpointTriggered.assign(CurrentRoute.Checkpoints.size(), false);
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
                    triggered = PointTriggered(prevPos, currPos, prevMapID, currMapID, cp);
    
                if (triggered && !checkpointTriggered[i] &&  // 6
                    (cp.TriggerType != ETriggerType::Circle || !wasInCheckpoint[i]))
                {
                    SpeedrunTimer.AddSplit(CurrentRoute.Checkpoints[i].Name);
                    checkpointTriggered[i] = true;
                }
    
                wasInCheckpoint[i] = (cp.TriggerType == ETriggerType::Circle && onCorrectMap)
                    ? IsWithinRange(currPos, cp)
                    : false;
            }

            // Goal trigger check
            bool goalTriggered = false;
            if (CurrentRoute.Goal.TriggerType == ETriggerType::AllCheckpoints)
            {
                // Fire once all checkpoints have been hit (order doesn't matter)
                bool allDone = !CurrentRoute.Checkpoints.empty();
                for (int i = 0; i < (int)checkpointTriggered.size(); i++)
                    if (!checkpointTriggered[i]) { allDone = false; break; }
                goalTriggered = allDone;
            }
            else
            {
                bool goalOnCorrectMap = CurrentRoute.Goal.MapID == 0 ||
                    CurrentRoute.Goal.TriggerType == ETriggerType::MapChange
                    ? true
                    : currMapID == CurrentRoute.Goal.MapID;
                goalTriggered = goalOnCorrectMap &&
                    PointTriggered(prevPos, currPos, prevMapID, currMapID, CurrentRoute.Goal);
            }

            if (goalTriggered)
            {
                SpeedrunTimer.Stop();
                GrandTimer.Stop();
                RunFinished = true;

                HistoricalRun run;
                run.Date       = GetCurrentDateTimeString();
                run.TotalTime  = SpeedrunTimer.GetElapsedSeconds();
                run.GrandTotal = GrandTimer.GetElapsedSeconds();
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

                SaveHistory(AddonDir, CurrentRouteName, BestSplits, HistoryRuns);
            }
        }
    }
    } // KeybindMutex

    prevPos = currPos;
    if (!isLoading)
        prevMapID = currMapID;
        
    InteractKeyPressed = false;

    RenderZones();
    RenderTimerOverlay();
    RenderConfigWindow();
    RenderHistoryWindow();
}

void AddonOptions()
{
    ImGui::Checkbox("Show Timer",         &ShowTimer);
    ImGui::Checkbox("Show Config Window", &ShowConfig);
    ImGui::Checkbox("Show History",       &ShowHistory);
    ImGui::Checkbox("Show Checkpoints",   &ShowZones);
    ImGui::Checkbox("Show Debug Overlay", &ShowDebug);
    ImGui::Separator();
    ImGui::Checkbox("Split Mode",         &SplitMode);
    ImGui::Checkbox("Compact Mode",       &CompactMode);
    ImGui::Checkbox("Show Grand Total", &ShowGrandTotal);
    ImGui::Separator();
    ImGui::Text("Max History Runs");
    ImGui::SetNextItemWidth(80.0f);
    ImGui::InputInt("##maxruns", &MaxHistoryRuns, 0, 0);
    if (MaxHistoryRuns < 1)   MaxHistoryRuns = 1;
    if (MaxHistoryRuns > 100) MaxHistoryRuns = 100;
    ImGui::Separator();
    if (ImGui::Button("Save Settings"))
        SaveSettings(AddonDir, GatherSettings());
}