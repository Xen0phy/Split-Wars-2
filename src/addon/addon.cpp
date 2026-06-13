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
//   - Migration notice popup (shown once after settings format change)

#include "render_shared.h"
#include "version.h"
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
        CheckpointState cp;
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
//
// NOTE: Does NOT handle the Circle-start "fires on exit" logic; that is
// handled directly in the per-checkpoint loop using wasInCircle.
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

    if (ShowSettingsMigrationNotice)
    {
        ImGui::SetNextWindowFocus();
        ImGui::SetNextWindowSize(ImVec2(400, 0), ImGuiCond_Always); // 0 height = auto-fit
        ImGui::SetNextWindowPos(
            ImVec2(ImGui::GetIO().DisplaySize.x * 0.5f,
                   ImGui::GetIO().DisplaySize.y * 0.5f),
            ImGuiCond_Always, ImVec2(0.5f, 0.5f));
    
        if (ImGui::Begin("##migration", nullptr,
            ImGuiWindowFlags_NoDecoration |
            ImGuiWindowFlags_NoMove       |
            ImGuiWindowFlags_AlwaysAutoResize))
        {
            ImGui::Spacing();
            ImGui::TextWrapped("Installation / Update Notice");
            ImGui::Spacing();
            char verBuf[32];
            snprintf(verBuf, sizeof(verBuf), "v%d.%d.%d.%d", Maj, Min, Bld, Rev);
            ImGui::TextWrapped("%s", verBuf);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
            ImGui::TextWrapped("%s", VersionNotice);
            ImGui::Spacing();
            ImGui::Separator();
            ImGui::Spacing();
    
            float btnW = ImGui::GetContentRegionAvail().x;
            if (ImGui::Button("Got it", ImVec2(btnW, 0)))
            {
                ShowSettingsMigrationNotice = false;
                SaveCurrentSettings();
            }
            ImGui::Spacing();
        }
        ImGui::End();
    }

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
    CheckpointState* startCp = GetStart(CurrentRoute);
    CheckpointState* goalCp  = GetGoal(CurrentRoute);

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
                    PendingGrandStop = GrandTimer.GetElapsedSeconds();
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
                if (PendingGrandStop >= 0.0 && goalCp && currMapID == goalCp->Point.MapID)
                    PendingGrandStop = -1.0;

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
            // Keep CheckpointStates in sync with the route.  Under normal operation
            // FullReset() already did this, but if checkpoints were added or removed
            // in the config editor mid-run we catch it here so we never go out of bounds.
            if (CheckpointStates.size() != CurrentRoute.Checkpoints.size())
                FullReset();

            // ── Unified per-checkpoint trigger loop ──────────────────────────────
            //
            // Every checkpoint — start, goal, intermediate — is evaluated in one
            // pass.  IsStart / IsGoal flags drive the timer start/stop actions;
            // the trigger type drives the geometry check.
            //
            // Start checkpoint (Circle only) special case:
            //   • Entering the circle → FullReset (arms the start).
            //   • Leaving the circle  → start the timer.
            //   All other trigger types on start fire immediately like a normal
            //   checkpoint, but also reset and start the timer instead of splitting.
            //
            // Goal checkpoint: on trigger → stop timers, record run.
            //
            // Intermediate checkpoint: on trigger → record split (once per run).
            // ─────────────────────────────────────────────────────────────────────
            for (int i = 0; i < (int)CheckpointStates.size(); i++)
            {
                CheckpointState& cs   = CheckpointStates[i];
                const RoutePoint& pt  = cs.Point;

                // Null types are decorative — skip all trigger logic.
                if (pt.TriggerType == ETriggerType::NullCircle ||
                    pt.TriggerType == ETriggerType::NullPlane)
                {
                    cs.wasInCircle = false;
                    continue;
                }

                // Map filter — AllCheckpoints and MapChange don't need a specific map.
                bool onCorrectMap = pt.TriggerType == ETriggerType::MapChange ||
                                    pt.TriggerType == ETriggerType::AllCheckpoints ||
                                    pt.MapID == 0 ||
                                    currMapID == pt.MapID;
                
                bool inCircle = onCorrectMap &&
                    (pt.TriggerType == ETriggerType::Circle         ||
                    pt.TriggerType == ETriggerType::CircleInteract ||
                    pt.TriggerType == ETriggerType::CombatArena) &&
                    IsWithinRange(currPos, pt);

                // ── START checkpoint ──────────────────────────────────────────────
                bool prevWasInCircle = cs.wasInCircle;
                if (cs.IsStart)
                {
                    if (pt.TriggerType == ETriggerType::Circle)
                    {
                        // Enter → reset (arms the start zone).
                        if (inCircle && !prevWasInCircle && SpeedrunTimer.IsRunning() && !cs.IsGoal)
                            FullReset();

                        // Exit → start timer.
                        if (!inCircle && prevWasInCircle &&
                            !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
                        {
                            SpeedrunTimer.Start();
                            GrandTimer.Start();
                        }

                        cs.wasInCircle = inCircle;
                    }
                    else if (pt.TriggerType == ETriggerType::Plane)
                    {
                        bool crossed = onCorrectMap && HasCrossedPlane(prevPos, currPos, pt);
                        if (crossed && !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
                        {
                            FullReset();
                            SpeedrunTimer.Start();
                            GrandTimer.Start();
                        }
                    }
                    else if (pt.TriggerType == ETriggerType::MapChange)
                    {
                        bool justLeft = (prevMapID == pt.MapID) && (currMapID != pt.MapID);
                        if (pt.MapID != 0 && justLeft &&
                            !SpeedrunTimer.IsRunning() && !SpeedrunTimer.IsFinished())
                        {
                            PendingStart = true;
                        }
                    }
                    else if (pt.TriggerType == ETriggerType::CircleInteract)
                    {
                        if (inCircle && InteractKeyPressed && !SpeedrunTimer.IsRunning())
                        {
                            FullReset();
                            SpeedrunTimer.Start();
                            GrandTimer.Start();
                        }
                        cs.wasInCircle = inCircle;
                    }
                    else if (pt.TriggerType == ETriggerType::CombatArena)
                    {
                        // Once a previous combat segment fully resolved, re-arm so the
                        // player can start another run without a manual reset.
                        if (cs.combat.finished && !SpeedrunTimer.IsRunning())
                            cs.combat = {};

                        // Detect whether this start and the goal share the exact same zone
                        // (single-arena run).  If so the goal's combat tracker must be armed
                        // at the same time as the start's.
                        CheckpointState* goalCs = GetGoal(CurrentRoute);
                        bool sameArea = goalCs &&
                            goalCs->Point.TriggerType == ETriggerType::CombatArena &&
                            goalCs->Point.MapID       == pt.MapID  &&
                            goalCs->Point.X           == pt.X      &&
                            goalCs->Point.Y           == pt.Y      &&
                            goalCs->Point.Z           == pt.Z      &&
                            goalCs->Point.RadiusWidth == pt.RadiusWidth;

                        if (!SpeedrunTimer.IsRunning())
                        {
                            bool risingEdge = currInCombat && !prevInCombat;
                            if (risingEdge && onCorrectMap && inCircle)
                            {
                                FullReset();
                                SpeedrunTimer.Start();
                                GrandTimer.Start();
                                // Re-fetch after FullReset (which re-syncs CheckpointStates).
                                CheckpointStates[i].combat.active = true;
                                CheckpointStates[i].combat.state  = ECombatState::Armed;

                                if (sameArea && goalCs)
                                {
                                    // NOTE: goalCs points into CurrentRoute, not CheckpointStates, so these
                                    // writes don't affect the live trigger state. In the same-area case,
                                    // CheckpointStates[i] is both start and goal — its combat struct is
                                    // already armed above and will be ticked when the loop falls through
                                    // to the goal block below. The goalCs writes are intentionally left
                                    // here only to arm the "Combat Start" split injection below.
                                    goalCs->combat.active = true;
                                    goalCs->combat.state  = ECombatState::Armed;
                                    // Inject "X Combat Start" for the goal so the taint scan
                                    // in the goal block has a valid anchor to stop at.
                                    char goalStartName[68];
                                    snprintf(goalStartName, sizeof(goalStartName), "%s Combat Start", goalCs->Name);
                                    SpeedrunTimer.AddSplit(goalStartName);
                                }
                            }
                        }
                        else if (SpeedrunTimer.IsRunning() && cs.combat.active)
                        {
                            if (!sameArea)
                            {
                                bool finished = TickCombat(cs.combat, pt, onCorrectMap, inCircle);
                                if (finished)
                                {
                                    double t = cs.combat.dropTime > 0.0
                                        ? cs.combat.dropTime
                                        : SpeedrunTimer.GetElapsedSeconds();
                                    Split s;
                                    snprintf(s.Name, sizeof(s.Name), "%s Combat End", cs.Name);
                                    s.Timestamp = t;
                                    SpeedrunTimer.AddSplitAt(s);
                                }
                            }
                        }

                        cs.wasInCircle = inCircle;
                    }

                    // If this checkpoint is both start and goal (e.g. a Circle zone that
                    // starts the timer on exit and also ends it), don't skip the goal
                    // evaluation block below — fall through so the goal can fire.
                    if (!cs.IsGoal)
                        continue; // Start-only checkpoint handled — move to next.
                }

                // ── GOAL and INTERMEDIATE checkpoints ────────────────────────────
                // Both are skipped during load screens.
                if (isLoading || !SpeedrunTimer.IsRunning())
                {
                    // Still track wasInCircle even when not running so we don't get
                    // a phantom entry edge on the frame the timer starts.
                    if (pt.TriggerType == ETriggerType::Circle         ||
                        pt.TriggerType == ETriggerType::CircleInteract ||
                        pt.TriggerType == ETriggerType::CombatArena)
                        cs.wasInCircle = inCircle;
                    continue;
                }

                bool fired = false; // Set to true when this checkpoint triggers this frame.

                if (pt.TriggerType == ETriggerType::CombatArena)
                {
                    // Arm on rising combat edge inside the zone.
                    if (!cs.combat.finished)
                    {
                        if (!cs.combat.active)
                        {
                            bool risingEdge = currInCombat && !prevInCombat;
                            if (risingEdge && inCircle)
                            {
                                cs.combat = { true, ECombatState::Armed };
                                char splitStartName[68];
                                snprintf(splitStartName, sizeof(splitStartName), "%s Combat Start", cs.Name);
                                SpeedrunTimer.AddSplit(splitStartName);
                            }
                        }
                        else
                        {
                            fired = TickCombat(cs.combat, pt, onCorrectMap, inCircle);
                        }
                    }
                }
                else if (pt.TriggerType == ETriggerType::AllCheckpoints)
                {
                    // AllCheckpoints: fires once every non-start/non-goal checkpoint
                    // has triggered at least once.
                    // NOTE: vacuously true (and fires immediately) if there are no
                    // intermediate checkpoints — a route with only start + AllCheckpoints
                    // goal is broken by design.
                    bool allDone = !CurrentRoute.Checkpoints.empty();
                    for (int j = 0; j < (int)CheckpointStates.size(); j++)
                    {
                        if (CheckpointStates[j].IsStart || CheckpointStates[j].IsGoal)
                            continue;
                        if (!CheckpointStates[j].triggered) { allDone = false; break; }
                    }
                    fired = allDone;
                }
                else
                {
                    // Circle (non-start): fires on the entering edge only.
                    // All other types use PointTriggered directly.
                    if (pt.TriggerType == ETriggerType::Circle)
                        fired = onCorrectMap && inCircle && !prevWasInCircle;
                    else
                        fired = onCorrectMap &&
                            PointTriggered(prevPos, currPos, prevMapID, currMapID, pt);
                }

                // Update wasInCircle for next frame.
                if (pt.TriggerType == ETriggerType::Circle         ||
                    pt.TriggerType == ETriggerType::CircleInteract ||
                    pt.TriggerType == ETriggerType::CombatArena)
                    cs.wasInCircle = inCircle;
                else
                    cs.wasInCircle = false;

                if (!fired || cs.triggered)
                    continue;

                // ── GOAL ─────────────────────────────────────────────────────────
                if (cs.IsGoal)
                {
                    // For CombatArena goals, check for a tainted run before accepting.
                    if (pt.TriggerType == ETriggerType::CombatArena)
                    {
                        double t = cs.combat.dropTime > 0.0
                            ? cs.combat.dropTime
                            : SpeedrunTimer.GetElapsedSeconds();

                        Split combatEnd;
                        combatEnd.Timestamp = t;
                        snprintf(combatEnd.Name, sizeof(combatEnd.Name), "%s Combat End", cs.Name);
                        SpeedrunTimer.AddSplitAt(combatEnd);

                        // Check for a tainted segment since the most recent Combat Start.
                        bool wasTainted = false;
                        char startName[68];
                        snprintf(startName, sizeof(startName), "%s Combat Start", cs.Name);
                        const auto& splits = SpeedrunTimer.GetSplits();
                        for (int si = (int)splits.size() - 1; si >= 0; si--)
                        {
                            if (strcmp(splits[si].Name, "__TAINTED__") == 0) { wasTainted = true; break; }
                            if (strcmp(splits[si].Name, startName)     == 0) break;
                        }

                        if (wasTainted)
                        {
                            cs.combat = {}; // Re-arm for next attempt.
                            continue;
                        }

                        // Clean run — fall through to the shared stop logic below,
                        // using the back-dated time.
                        SpeedrunTimer.StopAt(t);
                        GrandTimer.Stop();
                    }
                    else
                    {
                        SpeedrunTimer.Stop();
                        GrandTimer.Stop();
                    }

                    RunFinished = true;

                    HistoricalRun run;
                    run.Date       = GetCurrentDateTimeString();
                    run.TotalTime  = SpeedrunTimer.GetElapsedSeconds();
                    run.GrandTotal = (PendingGrandStop >= 0.0)
                        ? PendingGrandStop
                        : GrandTimer.GetElapsedSeconds();
                    DisplayedGrandTotal = run.GrandTotal;
                    PendingGrandStop    = -1.0;
                    run.Splits          = SpeedrunTimer.GetSplits();

                    // Ensure the goal checkpoint itself appears as the final split entry
                    // for plain trigger types.  CombatArena already injected "X Combat End".
                    if (pt.TriggerType != ETriggerType::CombatArena)
                    {
                        if (run.Splits.empty() || strcmp(run.Splits.back().Name, cs.Name) != 0)
                        {
                            Split goalSplit;
                            strncpy(goalSplit.Name, cs.Name, sizeof(goalSplit.Name) - 1);
                            goalSplit.Timestamp = run.TotalTime;
                            run.Splits.push_back(goalSplit);
                        }
                    }

                    HistoryRuns.insert(HistoryRuns.begin(), run);
                    if (BestRunIndex >= 0) BestRunIndex++;
                    TrimHistory();
                    if (!CurrentHistoryPath.empty())
                    {
                        UpdateSegments(run, SegmentRecords);
                        SaveHistory(CurrentHistoryPath, HistoryRuns, SegmentRecords, BestRunIndex);
                    }

                    cs.triggered = true;
                    break; // Run is over — no need to evaluate further checkpoints.
                }

                // ── INTERMEDIATE checkpoint ───────────────────────────────────────
                if (pt.TriggerType == ETriggerType::CombatArena)
                {
                    Split s;
                    snprintf(s.Name, sizeof(s.Name), "%s Combat End", cs.Name);
                    s.Timestamp = cs.combat.dropTime > 0.0
                        ? cs.combat.dropTime
                        : SpeedrunTimer.GetElapsedSeconds();
                    SpeedrunTimer.AddSplitAt(s);
                }
                else
                {
                    SpeedrunTimer.AddSplit(cs.Name);
                }
                cs.triggered = true;
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
            // Note for Claude: FullReset() also resets PendingStart
            if (PendingStart && !isLoading)
            {
                FullReset();
                SpeedrunTimer.Start();
                GrandTimer.Start();
            }

            // Update grand total display value for this frame.
            if (SpeedrunTimer.IsRunning())
                DisplayedGrandTotal = (PendingGrandStop >= 0.0) ? PendingGrandStop : GrandTimer.GetElapsedSeconds();

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
    else if (isCharSelect)
    {
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
    RenderSpeedoWindow();
}