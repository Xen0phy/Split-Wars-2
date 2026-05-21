#pragma once

#include <chrono>
#include <vector>

struct Split
{
    double      Timestamp   = 0.0; // time from start when this split was recorded
    char        Name[64]    = {};
};

class Timer
{
public:
    void    Start();
    void    Stop();
    void    Reset();
    void    Pause();
    void    Resume();
    bool    IsRunning()  const;
    bool    IsFinished() const;
    bool    IsPaused()   const;
    double  GetElapsedSeconds() const;
    void    AddSplit(const char* name);
    void    AddSplitAt(const Split& split); // insert a split with a pre-recorded timestamp
    void    StopAt(double elapsedSeconds);  // stop the timer at a specific elapsed time
    const   std::vector<Split>& GetSplits() const;

private:
    bool                                        m_Running   = false;
    bool                                        m_Finished  = false;
    bool                                        m_Paused    = false;
    double                                      m_PausedDuration = 0.0;
    std::chrono::steady_clock::time_point       m_StartTime;
    std::chrono::steady_clock::time_point       m_StopTime;
    std::chrono::steady_clock::time_point       m_PauseTime;
    std::vector<Split>                          m_Splits;
};