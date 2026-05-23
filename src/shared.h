// shared.h
#pragma once

#include "Nexus.h"
#include "Mumble.h"
#include "timer.h"
#include "route.h"
#include "storage.h"
#include <string>
#include <vector>
#include <atomic>
#include <mutex>

extern AddonAPI_t*                      APIDefs;
extern Mumble::Data*                    MumbleLink;
extern Timer                            SpeedrunTimer;
extern Timer                            GrandTimer;
extern Route                            CurrentRoute;
extern float                            CameraFOV;
extern bool                             ShowZones;
extern bool                             ShowTimer;
extern bool                             ShowConfig;
extern bool                             ShowDebug;
enum class TimerMode { Segment = 0, Split = 1, LiveSplit = 2 };
extern TimerMode                        TimerDisplayMode;
extern bool                             CompactMode;
extern bool                             ShowHistory;
extern bool                             ShowGrandTotal;
extern bool                             ShowRouteBrowser;
extern int                              MaxHistoryRuns;
extern std::vector<Split>               BestSplits;
extern std::vector<HistoricalRun>       HistoryRuns;
extern std::string                      CurrentRouteName;
extern std::string                      CurrentRouteFilepath;   // full path to the loaded .json
extern std::string                      CurrentHistoryPath;     // full path to the sibling .history
extern std::string                      AddonDir;
extern bool                             RunFinished;
extern double                           DisplayedGrandTotal;  // shown in overlay; frozen at goal for MapChange runs
extern bool                             PendingStart;
extern std::atomic<bool>                InteractKeyPressed;
extern std::mutex                       KeybindMutex;

// Runtime combat trigger states (parallel to route layout: [0]=Start, [1..N]=Checkpoints, [N+1]=Goal)
extern CombatTriggerState               CombatStart;
extern std::vector<CombatTriggerState>  CombatCheckpoints;
extern CombatTriggerState               CombatGoal;
extern std::vector<bool>                checkpointTriggered;
extern bool                             WasInCircleStart;
extern std::vector<bool>                WasInCheckpoint;

// Resets both timers, all trigger states, and all per-frame tracking.
// Call this wherever the run needs to start clean.
void FullReset();

// Hotbar (QuickAccess) hide-all toggle state
extern bool                             HotbarWindowsHidden;
extern bool                             HotbarSavedShowTimer;
extern bool                             HotbarSavedShowConfig;
extern bool                             HotbarSavedShowHistory;
extern bool                             HotbarSavedShowZones;
extern bool                             HotbarSavedShowRouteBrowser;
