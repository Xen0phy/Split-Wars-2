// shared.h
#pragma once

#include "Nexus.h"
#include "Mumble.h"
#include "timer.h"
#include "route.h"
#include "storage.h"
#include <string>
#include <vector>

extern AddonAPI_t*                  APIDefs;
extern Mumble::Data*                MumbleLink;
extern Timer                        SpeedrunTimer;
extern Timer                        GrandTimer;
extern Route                        CurrentRoute;
extern float                        CameraFOV;
extern bool                         ShowZones;
extern bool                         ShowTimer;
extern bool                         ShowConfig;
extern bool                         ShowDebug;
extern bool                         SplitMode;
extern bool                         CompactMode;
extern bool                         ShowHistory;
extern bool                         ShowGrandTotal;
extern int                          MaxHistoryRuns;
extern std::vector<Split>           BestSplits;
extern std::vector<HistoricalRun>   HistoryRuns;
extern std::string                  CurrentRouteName;
extern std::string                  AddonDir;
extern bool                         RunFinished;
extern bool                         PendingStart;
extern bool                         ReadyToStart;