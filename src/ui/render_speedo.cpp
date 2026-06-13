// render_speedo.cpp
// Speedometer overlay for Split Wars 2.
//
// Speed is derived each frame by comparing the player's world-space position
// (GS.PlayerX/Y/Z) against the previous frame's position and dividing by the
// elapsed wall-clock time. Both Mumble and RTAPI provide positions in meters,
// so the raw unit is meters per second (m/s).
//
// Unit conversion:
//   m/s → km/h : × 3.6
//   m/s → mph  : × 2.23694
//
// Speed is averaged over the last 100ms using a timestamp-based deque so
// the smoothing window is consistent regardless of framerate.
//
// Speed is suppressed during loading screens so the teleport at the end of a
// load screen doesn't produce a spurious spike.
//
// Two display modes (SpeedoDrawMode, persisted setting):
//   Numeric    — plain text number
//   Tachometer — 120° arc with filled sweep and needle

#include "render_shared.h"
#include "render_speedo.h"
#include <chrono>
#include <cmath>
#include <deque>

// ---------------------------------------------------------------------------
// Unit conversion constants
// ---------------------------------------------------------------------------
static constexpr float MPS_TO_KMH = 3.6f;
static constexpr float MPS_TO_MPH = 2.23694f;

// ---------------------------------------------------------------------------
// Tachometer geometry
// ---------------------------------------------------------------------------
// The arc spans 120 degrees, centered at the bottom of the circle.
// 0 km/h is at the left end, max speed at the right end.
//
// In ImGui's draw system, 0° is 3 o'clock and angles increase clockwise.
// We want the arc to sit like a speedometer — opening upward — so:
//   left end  (0 speed) = 210° = 7π/6
//   right end (max)     = 330° = 11π/6
// ---------------------------------------------------------------------------
static constexpr float ARC_START_DEG = 210.0f; // degrees, 0 speed
static constexpr float ARC_END_DEG   = 330.0f; // degrees, max speed
static constexpr float DEG_TO_RAD    = 3.14159265f / 180.0f;

static constexpr float MAX_SPEED_KMH = 200.0f;
static constexpr float MAX_SPEED_MPH = MAX_SPEED_KMH / 1.60934f; // ~124 mph

// ---------------------------------------------------------------------------
// SpeedoComputeSpeed  (shared between numeric and tachometer modes)
// ---------------------------------------------------------------------------
// Returns current smoothed speed in km/h or mph depending on SpeedUnitMph.
// Manages all static state internally.
// ---------------------------------------------------------------------------
static float SpeedoComputeSpeed()
{
    struct SpeedSample {
        float                                value;
        std::chrono::steady_clock::time_point time;
    };
    static std::deque<SpeedSample> s_speedSamples;

    static float s_prevX   = 0.0f;
    static float s_prevY   = 0.0f;
    static float s_prevZ   = 0.0f;
    static bool  s_hasPrev = false;

    using Clock = std::chrono::steady_clock;
    static Clock::time_point s_prevTime = Clock::now();

    Clock::time_point now = Clock::now();
    double dt = std::chrono::duration<double>(now - s_prevTime).count();
    s_prevTime = now;

    float displaySpeed = 0.0f;

    if (GS.IsLoading)
    {
        s_hasPrev = false;
        s_speedSamples.clear();
    }
    else if (s_hasPrev && dt > 0.0 && dt < 1.0)
    {
        float dx = GS.PlayerX - s_prevX;
        float dy = GS.PlayerY - s_prevY;
        float dz = GS.PlayerZ - s_prevZ;
        float distMPS = std::sqrt(dx*dx + dy*dy + dz*dz) / static_cast<float>(dt);
        float factor  = SpeedUnitMph ? MPS_TO_MPH : MPS_TO_KMH;
        float raw     = distMPS * factor;

        auto cutoff = now - std::chrono::milliseconds(100);

        if (raw < 0.1f)
            s_speedSamples.clear();
        else
            s_speedSamples.push_back({ raw, now });

        while (!s_speedSamples.empty() && s_speedSamples.front().time < cutoff)
            s_speedSamples.pop_front();

        if (!s_speedSamples.empty())
        {
            float sum = 0.0f;
            for (const auto& s : s_speedSamples) sum += s.value;
            displaySpeed = sum / static_cast<float>(s_speedSamples.size());
        }
    }

    s_prevX   = GS.PlayerX;
    s_prevY   = GS.PlayerY;
    s_prevZ   = GS.PlayerZ;
    s_hasPrev = true;

    return displaySpeed;
}

// ---------------------------------------------------------------------------
// RenderSpeedoWindow
// ---------------------------------------------------------------------------
void RenderSpeedoWindow()
{
    if (!ShowSpeedo) return;

    float speed = SpeedoComputeSpeed();

    const char* unitLabel = SpeedUnitMph ? "mph" : "km/h";
    float       maxSpeed  = SpeedUnitMph ? MAX_SPEED_MPH : MAX_SPEED_KMH;

    // -------------------------------------------------------------------------
    // Numeric mode
    // -------------------------------------------------------------------------
    if (!SpeedoTachometer)
    {
        ImGui::SetNextWindowPos(ImVec2(10, 200), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##speedo", nullptr,
            ImGuiWindowFlags_NoDecoration       |
            ImGuiWindowFlags_AlwaysAutoResize   |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav
        );
        ImGui::Text("%.0f %s", speed, unitLabel);
        ImGui::End();
        return;
    }

    // -------------------------------------------------------------------------
    // Tachometer mode
    // -------------------------------------------------------------------------
    // Transparent, borderless, fixed-size window.
    // The tach radius drives the window size; position defaults to center-ish.
    // -------------------------------------------------------------------------
    const float radius      = SpeedoRadius;        // persisted setting
    const float windowSize  = radius * 2.0f + 20.0f; // padding on each side

    // Default position: horizontally centered, 200px from the bottom.
    ImGuiIO& io = ImGui::GetIO();
    ImGui::SetNextWindowPos(
        ImVec2(io.DisplaySize.x * 0.5f - windowSize * 0.5f,
               io.DisplaySize.y - windowSize - 200.0f),
        ImGuiCond_FirstUseEver
    );
    ImGui::SetNextWindowSize(ImVec2(windowSize, windowSize), ImGuiCond_Always);
    ImGui::SetNextWindowBgAlpha(0.0f);
    ImGui::Begin("##speedo_tach", nullptr,
        ImGuiWindowFlags_NoDecoration       |
        ImGuiWindowFlags_NoFocusOnAppearing |
        ImGuiWindowFlags_NoNav              |
        ImGuiWindowFlags_NoScrollbar        |
        ImGuiWindowFlags_NoScrollWithMouse
    );

    ImDrawList* draw = ImGui::GetWindowDrawList();
    ImVec2      wPos = ImGui::GetWindowPos();

    // Centre of the arc in screen space.
    ImVec2 centre(wPos.x + windowSize * 0.5f, wPos.y + windowSize * 0.5f);

    static float s_needlePos = 0.0f; // current needle position 0..1
    static float s_needleVel = 0.0f; // current needle velocity

    float target     = std::fmin(speed / maxSpeed, 1.0f);
    float springK    = 12.0f;  // stiffness — how eagerly it chases the target
    float damping    = 6.0f;   // damping — higher = less overshoot/oscillation
    float dt_needle  = ImGui::GetIO().DeltaTime;

    float force      = (target - s_needlePos) * springK - s_needleVel * damping;
    s_needleVel     += force * dt_needle;
    s_needlePos     += s_needleVel * dt_needle;
    s_needlePos      = std::fmax(0.0f, std::fmin(s_needlePos, 1.0f));

    float t = s_needlePos;
    
    float arcStart = ARC_START_DEG * DEG_TO_RAD;
    float arcEnd   = ARC_END_DEG   * DEG_TO_RAD;
    float arcSpan  = arcEnd - arcStart;                  // positive = clockwise
    float needleAngle = arcStart + t * arcSpan;

    // --- Background arc (thin, dark) ---
    draw->PathArcTo(centre, radius, arcStart, arcEnd, 64);
    draw->PathStroke(IM_COL32(80, 80, 80, 200), false, 2.0f);

    // --- Filled sweep arc ---
    if (t > 0.0f)
    {
        draw->PathArcTo(centre, radius, arcStart, needleAngle, 64);
        draw->PathStroke(IM_COL32(0, 200, 255, 220), false, 4.0f);
    }

    // --- Needle (line from centre to arc) ---
    ImVec2 needleTip(
        centre.x + std::cos(needleAngle) * radius,
        centre.y + std::sin(needleAngle) * radius
    );
    draw->AddLine(centre, needleTip, IM_COL32(255, 255, 255, 230), 1.5f);

    // --- Speed label below centre ---
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f %s", speed, unitLabel);
    ImVec2 textSize = ImGui::CalcTextSize(buf);
    draw->AddText(
        ImVec2(centre.x - textSize.x * 0.5f, centre.y + radius * 0.3f),
        IM_COL32(255, 255, 255, 200),
        buf
    );

    ImGui::End();
}