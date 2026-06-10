// timer.cpp
// Implements the Timer class — a high-resolution stopwatch built on
// std::chrono::steady_clock that supports pausing, split recording,
// and back-dated stops.
//
// steady_clock is used rather than system_clock because it is guaranteed
// to be monotonic (never jumps backwards or adjusts for daylight saving),
// which is essential for accurate split times.

#include "timer.h"
#include "algorithm"
#include <cstring>

// ---------------------------------------------------------------------------
// Start
// ---------------------------------------------------------------------------
// Begins a fresh run.  No-ops if the timer is already running or has finished
// (call Reset() first if you want to start again from scratch).
// Clears any splits left over from a previous run.
// ---------------------------------------------------------------------------
void Timer::Start()
{
    if (m_Running || m_Finished) return;
    m_StartTime      = std::chrono::steady_clock::now();
    m_PausedDuration = 0.0;   // Cumulative load-screen time; starts at zero
    m_Running        = true;
    m_Finished       = false;
    m_Paused         = false;
    m_Splits.clear();
}

// ---------------------------------------------------------------------------
// Stop
// ---------------------------------------------------------------------------
// Stops the timer and marks it as finished.  Records the stop time so
// GetElapsedSeconds() returns the correct final value after the run ends.
// No-ops if the timer is not running.
// ---------------------------------------------------------------------------
void Timer::Stop()
{
    if (!m_Running) return;
    auto now = std::chrono::steady_clock::now();
    if (m_Paused)
        m_PausedDuration += std::chrono::duration<double>(now - m_PauseTime).count();
    m_StopTime = now;
    m_Running  = false;
    m_Finished = true;
    m_Paused   = false;
}

// ---------------------------------------------------------------------------
// Reset
// ---------------------------------------------------------------------------
// Returns the timer to its initial state: not running, not finished, no splits.
// Does not touch m_StartTime or m_StopTime — they are irrelevant once the
// flags are cleared.
// ---------------------------------------------------------------------------
void Timer::Reset()
{
    m_Running        = false;
    m_Finished       = false;
    m_Paused         = false;
    m_PausedDuration = 0.0;
    m_Splits.clear();
}

// ---------------------------------------------------------------------------
// Pause
// ---------------------------------------------------------------------------
// Freezes the timer while a loading screen is active.  Records the moment
// the pause began so Resume() can calculate the dead time to exclude.
// No-ops if the timer is not running or is already paused.
// ---------------------------------------------------------------------------
void Timer::Pause()
{
    if (!m_Running || m_Paused) return;
    m_PauseTime = std::chrono::steady_clock::now();
    m_Paused    = true;
}

// ---------------------------------------------------------------------------
// Resume
// ---------------------------------------------------------------------------
// Unfreezes the timer after a loading screen ends.  Adds the elapsed pause
// duration to m_PausedDuration so it is subtracted from all future elapsed
// time calculations, effectively excising the loading screen from the run.
// No-ops if the timer is not running or is not currently paused.
// ---------------------------------------------------------------------------
void Timer::Resume()
{
    if (!m_Running || !m_Paused) return;
    auto now = std::chrono::steady_clock::now();
    m_PausedDuration += std::chrono::duration<double>(now - m_PauseTime).count();
    m_Paused          = false;
}

// ---------------------------------------------------------------------------
// State queries
// ---------------------------------------------------------------------------
bool Timer::IsRunning()  const { return m_Running;  }
bool Timer::IsFinished() const { return m_Finished; }
bool Timer::IsPaused()   const { return m_Paused;   }

// ---------------------------------------------------------------------------
// GetElapsedSeconds
// ---------------------------------------------------------------------------
// Returns the net run time in seconds, excluding all paused (load screen) time.
//
// The "end" time point is chosen based on state:
//   • Timer running, not paused → current wall-clock time (live, ticking)
//   • Timer running, paused     → the moment the pause began (frozen display)
//   • Timer finished            → the recorded stop time (fixed final value)
//
// m_PausedDuration is then subtracted from the raw duration to remove all
// load screen time that accumulated over the course of the run.
// ---------------------------------------------------------------------------
double Timer::GetElapsedSeconds() const
{
    if (!m_Running && !m_Finished) return 0.0;

    auto end = m_Running
        ? (m_Paused ? m_PauseTime : std::chrono::steady_clock::now())
        : m_StopTime;

    return std::chrono::duration<double>(end - m_StartTime).count() - m_PausedDuration;
}

// ---------------------------------------------------------------------------
// AddSplit
// ---------------------------------------------------------------------------
// Records a named split at the current elapsed time.
// The timestamp is captured from GetElapsedSeconds() so it already has
// load screen time removed.
// ---------------------------------------------------------------------------
void Timer::AddSplit(const char* name)
{
    Split split;
    split.Timestamp = GetElapsedSeconds();
    strncpy(split.Name, name, sizeof(split.Name) - 1);
    m_Splits.push_back(split);
}

// ---------------------------------------------------------------------------
// AddSplitAt
// ---------------------------------------------------------------------------
// Records a pre-constructed Split directly, used when the split needs a
// back-dated timestamp (e.g. CombatArena triggers where the split time
// should reflect when combat dropped, not when the grace period expired).
// ---------------------------------------------------------------------------
void Timer::AddSplitAt(const Split& split)
{
    auto it = std::lower_bound(m_Splits.begin(), m_Splits.end(), split,
    [](const Split& a, const Split& b) { return a.Timestamp < b.Timestamp; });
    m_Splits.insert(it, split);
}

// ---------------------------------------------------------------------------
// StopAt
// ---------------------------------------------------------------------------
// Stops the timer at a specific elapsed time rather than right now.
// Used by CombatArena goal triggers to back-date the final stop to the
// moment combat actually dropped, not when the grace period timer expired.
//
// Rather than storing the elapsed value directly, we back-calculate the
// equivalent time_point so GetElapsedSeconds() continues to work correctly
// via the normal (m_StopTime - m_StartTime - m_PausedDuration) formula.
// ---------------------------------------------------------------------------
void Timer::StopAt(double elapsedSeconds)
{
    if (!m_Running) return;
    // Back-calculate: m_StopTime = m_StartTime + (elapsedSeconds + m_PausedDuration)
    // This ensures GetElapsedSeconds() returns exactly elapsedSeconds after the stop.
    m_StopTime = m_StartTime +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(elapsedSeconds + m_PausedDuration));
    m_Running  = false;
    m_Finished = true;
    m_Paused   = false;
}

// ---------------------------------------------------------------------------
// GetSplits
// ---------------------------------------------------------------------------
// Returns a const reference to the internal split list.  Callers read from
// this directly without copying — the reference remains valid as long as the
// Timer instance exists and Reset() is not called.
// ---------------------------------------------------------------------------
const std::vector<Split>& Timer::GetSplits() const
{
    return m_Splits;
}