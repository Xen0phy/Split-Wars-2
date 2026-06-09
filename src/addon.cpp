// addon.cpp
// Implements the per-frame render loop and all keybind callbacks.
//
// Responsibilities:
//   - Per-frame game state update (UpdateGameState)
//   - Load screen detection and timer pause/resume
//   - Trigger evaluation: start, checkpoint, and goal detection
//   - Dispatching to the individual UI renderer windows
//   - All keybind callback functions
//   - RegisterKeybinds() / DeregisterKeybinds() wrappers

#include "render_shared.h"
#include "worldrender.h"

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

// TrimHistory
// ---------------------------------------------------------------------------
// Removes the oldest unprotected runs until the list is within MaxHistoryRuns.
// Protected runs are: the designated best run (BestRunIndex) and the run with
// the fastest total time. These are never removed by automatic trimming.
// ---------------------------------------------------------------------------
void TrimHistory()
{
    if ((int)HistoryRuns.size() <= MaxHistoryRuns) return;

    // Find the index of the fastest run.
    int fastestIdx = -1;
    double fastestTime = -1.0;
    for (int i = 0; i < (int)HistoryRuns.size(); i++)
    {
        if (fastestTime < 0.0 || HistoryRuns[i].TotalTime < fastestTime)
        {
            fastestTime = HistoryRuns[i].TotalTime;
            fastestIdx  = i;
        }
    }

    // Remove oldest unprotected runs from the end until within cap.
    for (int i = (int)HistoryRuns.size() - 1;
         i >= 0 && (int)HistoryRuns.size() > MaxHistoryRuns;
         i--)
    {
        if (i == BestRunIndex || i == fastestIdx) continue;

        HistoryRuns.erase(HistoryRuns.begin() + i);

        // Adjust protected indices after erase.
        if (BestRunIndex > i) BestRunIndex--;
        if (fastestIdx   > i) fastestIdx--;
    }
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
        TrimHistory();         // Trim the list to the configured cap
        if (!CurrentHistoryPath.empty())
            SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
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

    bool anyVisible = ShowConfig || ShowHistory || ShowRouteBrowser;

    if (anyVisible)
    {
        // Save current visibility and hide everything
        HotbarSavedShowConfig       = ShowConfig;
        HotbarSavedShowHistory      = ShowHistory;
        HotbarSavedShowRouteBrowser = ShowRouteBrowser;
        ShowConfig       = false;
        ShowHistory      = false;
        ShowRouteBrowser = false;
        HotbarWindowsHidden = true;
    }
    else
    {
        // Restore saved visibility, or fall back to defaults if nothing was saved
        bool nothingToRestore = !HotbarSavedShowConfig && !HotbarSavedShowHistory &&
                                !HotbarSavedShowRouteBrowser;
        ShowConfig       = nothingToRestore ? true  : HotbarSavedShowConfig;
        ShowHistory      = nothingToRestore ? false : HotbarSavedShowHistory;
        ShowRouteBrowser = nothingToRestore ? false : HotbarSavedShowRouteBrowser;
        HotbarWindowsHidden = false;
        if (!ShowTimer) ShowTimer = true;
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
// Keybind registration table
// ---------------------------------------------------------------------------
// All keybinds are listed here in one place. RegisterKeybinds() and
// DeregisterKeybinds() iterate over this array, so adding a new keybind
// only requires a single entry here.
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
// RegisterKeybinds
// ---------------------------------------------------------------------------
// Registers every entry in the Keybinds table with Nexus.
// Called by AddonLoad() in entry.cpp. Defined here so the Keybinds array
// and all handler functions stay private to this file.
// ---------------------------------------------------------------------------
void RegisterKeybinds()
{
    for (auto& kb : Keybinds)
        APIDefs->InputBinds_RegisterWithStruct(kb.ID, kb.Fn, Keybind_t{});
}

// ---------------------------------------------------------------------------
// DeregisterKeybinds
// ---------------------------------------------------------------------------
// Deregisters every entry in the Keybinds table from Nexus.
// Called by AddonUnload() in entry.cpp to ensure no dangling function
// pointers remain after the DLL is unloaded.
// ---------------------------------------------------------------------------
void DeregisterKeybinds()
{
    for (auto& kb : Keybinds)
        APIDefs->InputBinds_Deregister(kb.ID);
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
    static bool         wasLoading       = false;
    static Vector3      prevPos          = {0, 0, 0};
    static unsigned int prevMapID        = 0;
    static bool         prevInCombat     = false;
    static std::chrono::steady_clock::time_point loadScreenStart;

    // RTAPI alive tracking — previous frame's alive state so we can detect
    // the dead→alive rising edge that triggers a clean combat end after revive.
    static bool prevIsAlive = true;

    // Mumble stillness tracking — records when the player last moved while a
    // grace period was running. Used to detect death via 5-second no-movement.
    static std::chrono::steady_clock::time_point lastMoveTime;
    static bool stillnessTimerActive = false;

    // Frame delta — time since last render call, used by the Mumble grace accumulator.
    static std::chrono::steady_clock::time_point prevFrameTime = std::chrono::steady_clock::now();
    auto   frameNow   = std::chrono::steady_clock::now();
    double frameDelta = std::chrono::duration<double>(frameNow - prevFrameTime).count();
    prevFrameTime     = frameNow;

    // Snapshot current game state for this frame.
    Vector3      currPos      = Vector3{GS.PlayerX, GS.PlayerY, GS.PlayerZ};
    unsigned int currMapID    = GS.MapID;
    bool         currInCombat = GS.IsInCombat;

    // RTAPI: derive alive/downed flags from CharacterState bitfield.
    // When Mumble is active these are always true (no such signal available).
    bool currIsAlive  = true;
    bool currIsDowned = false;
    if (GS.RTAPIAvailable && GS.ActiveSource == EDataSource::RTAPI && RTAPIData)
    {
        uint32_t state = (uint32_t)RTAPIData->CharacterState;
        currIsAlive  = (state & (uint32_t)RTAPI::ECharacterState::IsAlive)   != 0;
        currIsDowned = (state & (uint32_t)RTAPI::ECharacterState::IsDowned)  != 0;
    }

    // -------------------------------------------------------------------------
    // CombatArena trigger helper (lambda, defined inline for access to frame state)
    //
    // Advances a CombatTriggerState one frame and returns true when the
    // combat segment is considered finished.  A segment is "finished" when:
    //   • The player leaves the zone while still in combat, OR
    //   • The player drops combat inside the zone and a 2-second grace period
    //     expires without combat resuming (catches brief combat drops mid-fight)
    //
    // Death handling:
    //   RTAPI — fully dead (!IsAlive && !IsDowned) immediately injects a tainted
    //           split and keeps the timer running. When the player is revived
    //           (IsAlive rising edge) combat end fires as a clean finish.
    //   Mumble — no alive signal available; uses a movement heuristic instead.
    //           The first 0.5s of grace ignores left-circle exits (covers the
    //           death-teleport window). If the player stops moving for 5 seconds
    //           during grace, a tainted split is injected and the segment resolves.
    //           Grace only counts down while the player is moving.
    //
    // Returns true when the segment resolves (clean or tainted).
    // NOTE: this function only advances the Armed → GracePending → finished
    // state machine. Recording "Combat Start" and "Combat End" splits is
    // the caller's responsibility.
    // -------------------------------------------------------------------------
    auto TickCombat = [&](CombatTriggerState& cs, const RoutePoint& point,
                          bool onCorrectMap, bool inCircle) -> bool
    {
        constexpr double GraceDuration       = 2.0;  // seconds — clean combat-drop window
        constexpr double StillnessTimeout    = 5.0;  // seconds — Mumble dead heuristic
        constexpr double ExitIgnoreWindow    = 0.5;  // seconds — ignore left-circle after grace starts
        constexpr float  MovementThreshold   = 0.001f; // metres — minimum delta to count as moving

        if (cs.finished) return false;

        // -----------------------------------------------------------------
        // RTAPI path: handle revive rising edge regardless of cs.active.
        // If the player was dead last frame and is alive this frame, fire a
        // clean combat end so the timer advances past the tainted segment.
        // -----------------------------------------------------------------
        if (GS.ActiveSource == EDataSource::RTAPI)
        {
            bool revivedThisFrame = currIsAlive && !prevIsAlive;
            if (revivedThisFrame && cs.active && cs.taintedPending)
            {
                cs.active        = false;
                cs.finished      = true;
                cs.taintedPending = false;
                return true; // clean finish after revive
            }

            // Fully dead while the segment is active → inject tainted, keep running.
            if (cs.active && !currIsAlive && !cs.taintedPending)
            {
                cs.taintedPending = true;
                Split taint;
                taint.Timestamp = SpeedrunTimer.GetElapsedSeconds();
                strncpy(taint.Name, "__TAINTED__", sizeof(taint.Name) - 1);
                SpeedrunTimer.AddSplitAt(taint);
                return false;
            }

            // While dead, suppress all other resolution paths.
            if (cs.active && !currIsAlive) return false;
        }

        // -----------------------------------------------------------------
        // Not yet active — wait for a rising combat edge inside the zone.
        // -----------------------------------------------------------------
        if (!cs.active)
        {
            bool risingEdge = currInCombat && !prevInCombat;
            if (risingEdge && onCorrectMap && inCircle)
            {
                cs.active            = true;
                cs.state             = ECombatState::Armed;
                lastMoveTime         = std::chrono::steady_clock::now();
                stillnessTimerActive = false;
            }
            return false;
        }

        // -----------------------------------------------------------------
        // Armed: player is in combat inside the zone.
        // -----------------------------------------------------------------
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
                cs.state             = ECombatState::GracePending;
                cs.dropTime          = SpeedrunTimer.GetElapsedSeconds();
                cs.graceStart        = std::chrono::steady_clock::now();
                cs.graceAccum        = 0.0;
                lastMoveTime         = std::chrono::steady_clock::now();
                stillnessTimerActive = true;
            }
            return false;
        }

        // -----------------------------------------------------------------
        // GracePending: combat dropped, waiting to see if it comes back.
        // -----------------------------------------------------------------
        else if (cs.state == ECombatState::GracePending)
        {
            auto now = std::chrono::steady_clock::now();
            // Wall-clock elapsed — used only for ExitIgnoreWindow (always real time).
            double wallElapsed = std::chrono::duration<double>(now - cs.graceStart).count();

            // --- Mumble movement / stillness heuristic ---
            // Only runs when Mumble is the active source (RTAPI handles death above).
            if (GS.ActiveSource != EDataSource::RTAPI)
            {
                float dx = currPos.X - prevPos.X;
                float dy = currPos.Y - prevPos.Y;
                float dz = currPos.Z - prevPos.Z;
                bool moving = (dx*dx + dy*dy + dz*dz) > (MovementThreshold * MovementThreshold);

                if (moving)
                {
                    // Accumulate grace time only while the player is moving.
                    cs.graceAccum += frameDelta;
                    lastMoveTime   = now;
                }
                else if (stillnessTimerActive)
                {
                    // Check if the player has been still long enough to be dead.
                    double stillSeconds = std::chrono::duration<double>(now - lastMoveTime).count();
                    if (stillSeconds >= StillnessTimeout)
                    {
                        Split taint;
                        taint.Timestamp = cs.dropTime;
                        strncpy(taint.Name, "__TAINTED__", sizeof(taint.Name) - 1);
                        SpeedrunTimer.AddSplitAt(taint);
                        cs.active            = false;
                        cs.finished          = true;
                        stillnessTimerActive = false;
                        return true;
                    }
                }
            }

            // Still out of combat and inside the zone — check if grace period expired.
            // RTAPI: use wall-clock elapsed. Mumble: use graceAccum (moving-time only).
            if (!currInCombat && inCircle)
            {
                double graceElapsed = (GS.ActiveSource != EDataSource::RTAPI)
                    ? cs.graceAccum
                    : wallElapsed;

                if (graceElapsed >= GraceDuration)
                {
                    cs.active            = false;
                    cs.finished          = true;
                    stillnessTimerActive = false;
                    return true;
                }
                return false;
            }

            // Re-entered combat before grace expired — go back to Armed.
            if (currInCombat && inCircle)
            {
                cs.state             = ECombatState::Armed;
                cs.dropTime          = 0.0;
                cs.graceAccum        = 0.0;
                stillnessTimerActive = false;
                return false;
            }

            // Left the zone during grace period.
            // Ignore exits within the first ExitIgnoreWindow seconds (wall-clock) —
            // covers the death-teleport case where the game ports the character out.
            if (wallElapsed < ExitIgnoreWindow)
                return false;

            cs.active            = false;
            cs.finished          = true;
            stillnessTimerActive = false;
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
                // On the frame a load screen clears, prevPos is stale (it reflects a
                // position from before the load). Snap it to currPos so movement-delta
                // triggers (Plane) can't fire due to a phantom crossing on the first frame.
                prevPos = currPos;

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
                        goalCp->Point.RadiusWidth == startPt.RadiusWidth;

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

                    // Null types are decorative — never trigger.
                    if (cp.TriggerType == ETriggerType::NullCircle ||
                        cp.TriggerType == ETriggerType::NullPlane)
                        continue;

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
                        if (cp.TriggerType == ETriggerType::CombatArena)
                        {
                            // Always record "X Combat End" for CombatArena checkpoints.
                            // Back-date to dropTime when available (clean grace-period finish).
                            // On the RTAPI revive path the player died while Armed, so
                            // GracePending was never entered and dropTime is 0 — in that
                            // case use the current elapsed time instead.
                            Split s;
                            snprintf(s.Name, sizeof(s.Name), "%s Combat End", CurrentRoute.Checkpoints[i].Name);
                            s.Timestamp = CombatCheckpoints[i].dropTime > 0.0
                                ? CombatCheckpoints[i].dropTime
                                : SpeedrunTimer.GetElapsedSeconds();
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
                            startCp->Point.RadiusWidth == goalPt.RadiusWidth;

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
                                bool combatFinished = TickCombat(CombatGoal, goalPt, onCorrectMap, inCircle);
                                if (combatFinished)
                                {
                                    double t = CombatGoal.dropTime > 0.0
                                        ? CombatGoal.dropTime
                                        : SpeedrunTimer.GetElapsedSeconds();

                                    // Always inject the Combat End split so it appears in the timer.
                                    Split s;
                                    s.Timestamp = t;
                                    snprintf(s.Name, sizeof(s.Name), "%s Combat End", goalCp->Name);
                                    SpeedrunTimer.AddSplitAt(s);

                                    // Check whether a __TAINTED__ split exists after the most recent
                                    // Combat Start for this goal — covers both RTAPI and Mumble paths.
                                    bool wasTainted = false;
                                    char startName[68];
                                    snprintf(startName, sizeof(startName), "%s Combat Start", goalCp->Name);
                                    const auto& splits = SpeedrunTimer.GetSplits();
                                    for (int si = (int)splits.size() - 1; si >= 0; si--)
                                    {
                                        if (strcmp(splits[si].Name, "__TAINTED__") == 0) { wasTainted = true; break; }
                                        if (strcmp(splits[si].Name, startName)     == 0) break; // reached start, no tainted
                                    }

                                    if (!wasTainted)
                                    {
                                        goalTriggered = true;
                                        goalTime      = t;
                                    }
                                    else
                                    {
                                        // Reset goal tracker so the user can re-arm.
                                        CombatGoal = {};
                                    }
                                }
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
                        // CombatArena goals already injected a "X Combat End" split — skip.
                        if (goalPt.TriggerType != ETriggerType::CombatArena)
                        {
                            if (run.Splits.empty() || strcmp(run.Splits.back().Name, goalCp->Name) != 0)
                            {
                                Split goalSplit;
                                strncpy(goalSplit.Name, goalCp->Name, sizeof(goalSplit.Name) - 1);
                                goalSplit.Timestamp = run.TotalTime;
                                run.Splits.push_back(goalSplit);
                            }
                        }

                        HistoryRuns.insert(HistoryRuns.begin(), run);
                        if (BestRunIndex >= 0)
                            BestRunIndex++;
                        TrimHistory();
                        if (!CurrentHistoryPath.empty())
                        {
                            UpdateSegments(run, SegmentRecords);
                            SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
                        }
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
    bool isCharSelect = GS.RTAPIAvailable && RTAPIData &&
        RTAPIData->GameState == RTAPI::EGameState::CharacterSelection;
    if (!isLoading)
    {
        prevMapID = currMapID;
    }
    else if (isCharSelect) {
        prevMapID = 0;
    }
    prevInCombat       = currInCombat;
    prevIsAlive        = currIsAlive;
    InteractKeyPressed = false; // Consumed — clear for next frame

    // --- Draw overlays ---
    // Skip world and timer rendering on the character selection screen
    // (detected by player position being exactly zero).
    RenderZones();
    if (StreamerMode)
        RenderTimerOverlayStream();
    else
        RenderTimerOverlay();
    RenderConfigWindow();
    RenderHistoryWindow();
    RenderRouteBrowserWindow();
    RenderDebugWindow();
}