// timer.h
// Declaration of the Timer class and the Split struct.
//
// Two Timer instances run in parallel during a speedrun:
//   SpeedrunTimer — paused during load screens; this is the "official" run time.
//   GrandTimer    — never paused; records wall-clock time including load screens.
//
// The state machine has three mutually exclusive states:
//   Idle     — not running, not finished  (before Start() or after Reset())
//   Running  — actively counting          (after Start(), optionally Paused)
//   Finished — stopped with a final time  (after Stop() or StopAt())

#pragma once

#include <chrono>
#include <vector>

// ---------------------------------------------------------------------------
// Split
// ---------------------------------------------------------------------------
// A single recorded split: the elapsed run time at the moment the split fired
// and a display name (checkpoint name, "Manual Stop", "Goal", etc.).
//
// Timestamp is in seconds relative to the start of the run, with load screen
// time already excluded (it comes from Timer::GetElapsedSeconds()).
// Name is a fixed-size buffer — 64 characters is enough for any checkpoint
// name and avoids heap allocation per split.
// ---------------------------------------------------------------------------
struct Split
{
    double Timestamp = 0.0; // Seconds from run start (load screens excluded)
    char   Name[64]  = {};
};

// ---------------------------------------------------------------------------
// Timer
// ---------------------------------------------------------------------------
// High-resolution stopwatch built on std::chrono::steady_clock.
// steady_clock is monotonic — it never jumps backwards or skips forward for
// daylight saving — making it safe for precise elapsed-time measurements.
//
// Pausing accumulates dead time in m_PausedDuration, which is subtracted
// from all elapsed time calculations so load screens are invisible to the
// run time.  The grand total timer uses the same class but is simply never
// paused, giving the wall-clock total automatically.
// ---------------------------------------------------------------------------
class Timer
{
public:
    // --- Lifecycle ---

    // Start a fresh run. No-op if already running or finished;
    // call Reset() first to start again.
    void Start();

    // Stop the timer at the current moment and mark it as finished.
    void Stop();

    // Return to the initial idle state and clear all splits.
    void Reset();

    // --- Load screen support ---

    // Freeze elapsed time (called when a loading screen begins).
    void Pause();

    // Resume elapsed time and accumulate the paused duration (called when
    // the loading screen ends).
    void Resume();

    // --- State queries ---
    bool IsRunning()  const;
    bool IsFinished() const;
    bool IsPaused()   const;

    // --- Time ---

    // Returns net elapsed seconds, excluding all paused (load screen) time.
    // Returns 0.0 when idle.  Freezes at the final value once finished.
    double GetElapsedSeconds() const;

    // --- Split recording ---

    // Record a named split at the current elapsed time.
    void AddSplit(const char* name);

    // Record a pre-constructed split with a back-dated timestamp.
    // Used by CombatArena triggers where the split time should reflect
    // when combat dropped, not when the grace period expired.
    void AddSplitAt(const Split& split);

    // Stop the timer at a specific elapsed time (back-dated stop).
    // Used by CombatArena goal triggers so the final time reflects when
    // combat actually ended rather than when the grace period timer fired.
    void StopAt(double elapsedSeconds);

    // Returns a const reference to the recorded splits.  Valid until
    // Reset() is called.
    const std::vector<Split>& GetSplits() const;

private:
    bool   m_Running         = false;
    bool   m_Finished        = false;
    bool   m_Paused          = false;

    // Cumulative seconds spent paused (load screens); subtracted from all
    // elapsed time calculations so load screen time is excluded.
    double m_PausedDuration  = 0.0;

    std::chrono::steady_clock::time_point m_StartTime; // Set by Start()
    std::chrono::steady_clock::time_point m_StopTime;  // Set by Stop() or StopAt()
    std::chrono::steady_clock::time_point m_PauseTime; // Set by Pause(); used by Resume()

    std::vector<Split> m_Splits;
};