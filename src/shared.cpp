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
bool                            SplitMode           = true;
bool                            CompactMode         = false;
bool                            ShowHistory         = false;
bool                            ShowGrandTotal      = false;
int                             MaxHistoryRuns      = 10;
std::vector<Split>              BestSplits;
std::vector<HistoricalRun>      HistoryRuns;
std::string                     CurrentRouteName    = "New Route";
std::string                     AddonDir;
bool                            RunFinished         = false;
bool                            PendingStart        = false;
std::atomic<bool>               InteractKeyPressed  = false;
std::mutex                      KeybindMutex;

CombatTriggerState              CombatStart;
std::vector<CombatTriggerState> CombatCheckpoints;
CombatTriggerState              CombatGoal;
std::vector<bool>               checkpointTriggered;