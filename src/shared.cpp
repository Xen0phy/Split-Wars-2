// shared.cpp
#include "shared.h"

AddonAPI_t*                     APIDefs             = nullptr;
Mumble::Data*                   MumbleLink          = nullptr;
Timer                           SpeedrunTimer;
Timer                           GrandTimer;
Route                           CurrentRoute;
float                           CameraFOV           = 0.873f;
bool                            ShowZones           = true;
bool                            ShowTimer           = true;
bool                            ShowConfig          = true;
bool                            ShowDebug           = false;
TimerMode                       TimerDisplayMode     = TimerMode::Split;
bool                            CompactMode         = false;
bool                            ShowHistory         = false;
bool                            ShowGrandTotal      = false;
bool                            ShowRouteBrowser    = false;
int                             MaxHistoryRuns      = 10;
std::vector<Split>              BestSplits;
std::vector<HistoricalRun>      HistoryRuns;
std::string                     CurrentRouteName    = "New Route";
std::string                     CurrentRouteFilepath;
std::string                     CurrentHistoryPath;
std::string                     AddonDir;
bool                            RunFinished         = false;
double                          DisplayedGrandTotal = 0.0;
bool                            PendingStart        = false;
std::atomic<bool>               InteractKeyPressed  = false;
std::mutex                      KeybindMutex;

CombatTriggerState              CombatStart;
std::vector<CombatTriggerState> CombatCheckpoints;
CombatTriggerState              CombatGoal;
std::vector<bool>               checkpointTriggered;
bool                            WasInCircleStart    = false;
std::vector<bool>               WasInCheckpoint;

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

bool                            HotbarWindowsHidden      = false;
bool                            HotbarSavedShowTimer     = false;
bool                            HotbarSavedShowConfig    = false;
bool                            HotbarSavedShowHistory   = false;
bool                            HotbarSavedShowZones     = false;
bool                            HotbarSavedShowRouteBrowser = false;
