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

extern "C" __declspec(dllexport) AddonDefinition_t* GetAddonDef()
{
    AddonDef.Signature        = -1;
    AddonDef.APIVersion       = NEXUS_API_VERSION;
    AddonDef.Name             = "GW2 Speedrun";
    AddonDef.Version.Major    = 0;
    AddonDef.Version.Minor    = 1;
    AddonDef.Version.Build    = 0;
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

    // Load settings
    Settings s;
    if (LoadSettings(AddonDir, s))
        ApplySettings(s);

    APIDefs->GUI_Register(RT_Render, AddonRender);
    APIDefs->GUI_Register(RT_OptionsRender, AddonOptions);
    APIDefs->Events_Subscribe("EV_MUMBLE_IDENTITY_UPDATED", HandleIdentityUpdate);
}

void AddonUnload()
{
    SaveSettings(AddonDir, GatherSettings());

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
        case ETriggerType::Circle:
        default:
            return IsWithinRange(currPos, point);
    }
}

void AddonRender()
{
    if (!MumbleLink) return;

    static unsigned int lastUITick = 0;
    static bool         wasLoading = false;
    bool isLoading = (MumbleLink->UITick == lastUITick);
    lastUITick     = MumbleLink->UITick;

    if (isLoading && !wasLoading)
        SpeedrunTimer.Pause();
    else if (!isLoading && wasLoading)
        SpeedrunTimer.Resume();
    wasLoading = isLoading;

    if (CurrentRoute.IsValid && !isLoading)
    {
        static Vector3      prevPos   = {0, 0, 0};
        static unsigned int prevMapID = 0;

        Vector3      currPos   = MumbleLink->AvatarPosition;
        unsigned int currMapID = MumbleLink->Context.MapID;

        static bool wasInStart = false;
        static std::vector<bool> wasInCheckpoint;

        if (wasInCheckpoint.size() != CurrentRoute.Checkpoints.size())
            wasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);

        bool inStart = IsWithinRange(currPos, CurrentRoute.Start);

        if (inStart && !wasInStart)
        {
            SpeedrunTimer.Reset();
            RunFinished = false;
            wasInCheckpoint.assign(CurrentRoute.Checkpoints.size(), false);
        }

        if (!inStart && wasInStart && !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
            SpeedrunTimer.Start();

        if (SpeedrunTimer.IsRunning())
        {
            for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
            {
                const RoutePoint& cp = CurrentRoute.Checkpoints[i].Point;
                bool triggered = PointTriggered(prevPos, currPos, prevMapID, currMapID, cp);

                if (triggered && (cp.TriggerType != ETriggerType::Circle || !wasInCheckpoint[i]))
                    SpeedrunTimer.AddSplit(CurrentRoute.Checkpoints[i].Name);

                wasInCheckpoint[i] = cp.TriggerType == ETriggerType::Circle
                    ? IsWithinRange(currPos, cp)
                    : false;
            }

            if (PointTriggered(prevPos, currPos, prevMapID, currMapID, CurrentRoute.Goal))
            {
                SpeedrunTimer.Stop();
                RunFinished = true;

                // Auto-save to history
                HistoricalRun run;
                run.Date      = GetCurrentDateTimeString();
                run.TotalTime = SpeedrunTimer.GetElapsedSeconds();
                run.Splits    = SpeedrunTimer.GetSplits();

                // Add goal split if not already there
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

        wasInStart = inStart;
        prevPos    = currPos;
        prevMapID  = currMapID;
    }

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
    ImGui::Separator();
    ImGui::Text("Max History Runs");
    ImGui::SetNextItemWidth(60.0f);
    ImGui::InputInt("##maxruns", &MaxHistoryRuns, 0, 0);
    if (MaxHistoryRuns < 1)  MaxHistoryRuns = 1;
    if (MaxHistoryRuns > 100) MaxHistoryRuns = 100;
    ImGui::Separator();
    if (ImGui::Button("Save Settings"))
        SaveSettings(AddonDir, GatherSettings());
}