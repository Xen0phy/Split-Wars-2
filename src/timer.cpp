#include "timer.h"
#include <cstring>

void Timer::Start()
{
    if (m_Running || m_Finished) return;
    m_StartTime      = std::chrono::steady_clock::now();
    m_PausedDuration = 0.0;
    m_Running        = true;
    m_Finished       = false;
    m_Paused         = false;
    m_Splits.clear();
}

void Timer::Stop()
{
    if (!m_Running) return;
    m_StopTime = std::chrono::steady_clock::now();
    m_Running  = false;
    m_Finished = true;
    m_Paused   = false;
}

void Timer::Reset()
{
    m_Running        = false;
    m_Finished       = false;
    m_Paused         = false;
    m_PausedDuration = 0.0;
    m_Splits.clear();
}

void Timer::Pause()
{
    if (!m_Running || m_Paused) return;
    m_PauseTime = std::chrono::steady_clock::now();
    m_Paused    = true;
}

void Timer::Resume()
{
    if (!m_Running || !m_Paused) return;
    auto now          = std::chrono::steady_clock::now();
    m_PausedDuration += std::chrono::duration<double>(now - m_PauseTime).count();
    m_Paused          = false;
}

bool Timer::IsRunning() const
{
    return m_Running;
}

bool Timer::IsFinished() const
{
    return m_Finished;
}

bool Timer::IsPaused() const
{
    return m_Paused;
}

double Timer::GetElapsedSeconds() const
{
    if (!m_Running && !m_Finished) return 0.0;

    auto end = m_Running
        ? (m_Paused ? m_PauseTime : std::chrono::steady_clock::now())
        : m_StopTime;

    return std::chrono::duration<double>(end - m_StartTime).count() - m_PausedDuration;
}

void Timer::AddSplit(const char* name)
{
    Split split;
    split.Timestamp = GetElapsedSeconds();
    strncpy(split.Name, name, sizeof(split.Name) - 1);
    m_Splits.push_back(split);
}

void Timer::AddSplitAt(const Split& split)
{
    m_Splits.push_back(split);
}

void Timer::StopAt(double elapsedSeconds)
{
    if (!m_Running) return;
    // Back-calculate what the stop time_point would be for the given elapsed value
    m_StopTime = m_StartTime +
        std::chrono::duration_cast<std::chrono::steady_clock::duration>(
            std::chrono::duration<double>(elapsedSeconds + m_PausedDuration));
    m_Running  = false;
    m_Finished = true;
    m_Paused   = false;
}

const std::vector<Split>& Timer::GetSplits() const
{
    return m_Splits;
}