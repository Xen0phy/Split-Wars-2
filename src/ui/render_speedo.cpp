// render_speedo.cpp
// Speedometer overlay for Split Wars 2.
//
// ─── Tachometer geometry ────────────────────────────────────────────────────
//
//   SpeedoArcAngle  — total sweep of the arc in degrees (1-359)
//   SpeedoArcLength — total length of the arc in pixels
//   SpeedoAngle     — rotation of the whole speedo (0-360)
//   SpeedoPDistance — distance of needle origin P from the arc (0 = on arc, max 200px)
//
//   radius   = SpeedoArcLength / (SpeedoArcAngle * DEG_TO_RAD)
//   pDist    = radius - min(SpeedoPDistance, min(200, radius))
//   arcMid   = SpeedoAngle axis direction
//   arcStart = arcMid - ArcAngle/2
//   arcEnd   = arcMid + ArcAngle/2
//   needle   = line from P to arc point at current speed
//
// ─── Render modes ───────────────────────────────────────────────────────────
//
//   Edit mode   — draws inside an ImGui window so the user can drag to position it.
//                 Window position is saved to SpeedoWindowX/Y.
//   Play mode   — draws directly to ImGui background draw list at the saved position,
//                 so no ImGui window occludes world rendering.
//
// ────────────────────────────────────────────────────────────────────────────

#include "render_shared.h"
#include "render_speedo.h"
#include <chrono>
#include <cmath>
#include <deque>
#include <algorithm>
#include <functional>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr float MPS_TO_KMH  = 3.6f;
static constexpr float MPS_TO_MPH  = 2.23694f;
static constexpr float MAX_SPEED_KMH = 200.0f;
static constexpr float MAX_SPEED_MPH = MAX_SPEED_KMH / 1.60934f;
static constexpr float DEG_TO_RAD  = 3.14159265f / 180.0f;
static constexpr float PI          = 3.14159265f;
static constexpr float TWO_PI      = 6.28318530f;
static constexpr int   ARC_SEGMENTS = 64;

// ---------------------------------------------------------------------------
// SpeedoComputeSpeed
// ---------------------------------------------------------------------------
static float SpeedoComputeSpeed()
{
    struct SpeedSample {
        float                                 value;
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
    double            dt  = std::chrono::duration<double>(now - s_prevTime).count();
    s_prevTime            = now;
    float displaySpeed    = 0.0f;

    if (GS.IsLoading)
    {
        s_hasPrev = false;
        s_speedSamples.clear();
    }
    else if (s_hasPrev && dt > 0.0 && dt < 1.0)
    {
        float dx      = GS.PlayerX - s_prevX;
        float dy      = GS.PlayerY - s_prevY;
        float dz      = GS.PlayerZ - s_prevZ;
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
// DrawSpeedoContent
// Draws arc, sweep, needle and label onto any ImDrawList.
// toScreen converts local coords (C at origin) to screen space.
// ---------------------------------------------------------------------------
static void DrawSpeedoContent(
    ImDrawList*                          draw,
    std::function<ImVec2(float, float)>  toScreen,
    float                                t,
    float                                speed,
    const char*                          unitLabel,
    float                                radius,
    float                                pDist,
    float                                axisAngleRad,
    float                                arcStart,
    float                                arcEnd,
    float                                arcMidAngle,
    float                                needleAngle,
    bool                                 editMode)
{
    ImVec2 C_screen = toScreen(0.0f, 0.0f);
    ImVec2 P_screen = toScreen(
        std::cos(axisAngleRad) * pDist,
        std::sin(axisAngleRad) * pDist);

    // Background arc
    draw->PathArcTo(C_screen, radius, arcStart, arcEnd, ARC_SEGMENTS);
    draw->PathStroke(IM_COL32(80, 80, 80, 200), false, SpeedoArcBgWidth);

    // Filled sweep
    if (t > 0.0f)
    {
        draw->PathArcTo(C_screen, radius, arcStart, needleAngle, ARC_SEGMENTS);
        draw->PathStroke(IM_COL32(0, 200, 255, 220), false, SpeedoArcWidth);
    }

    // Needle from P to arc point
    if (SpeedoNeedleVisible)
    {
        ImVec2 needleTip(
            C_screen.x + std::cos(needleAngle) * radius,
            C_screen.y + std::sin(needleAngle) * radius);
        draw->AddLine(P_screen, needleTip, IM_COL32(255, 255, 255, 230), SpeedoNeedleWidth);
    }

    // Speed label near arc apex
    if (SpeedoLabelVisible)
    {
        char   buf[16];
        snprintf(buf, sizeof(buf), "%.0f %s", speed, unitLabel);
        ImVec2 textSize = ImGui::CalcTextSize(buf);
        ImVec2 apex     = toScreen(
            std::cos(arcMidAngle) * radius,
            std::sin(arcMidAngle) * radius);
        draw->AddText(
            ImVec2(apex.x - textSize.x * 0.5f, apex.y + 8.0f),
            IM_COL32(255, 255, 255, 200),
            buf);
    }

    // Edit mode markers
    if (editMode)
    {
        draw->AddCircleFilled(C_screen, 4.0f, IM_COL32(255, 200, 0, 255));
        draw->AddText(ImVec2(C_screen.x + 6, C_screen.y - 8), IM_COL32(255, 200, 0, 255), "C");
        draw->AddCircleFilled(P_screen, 4.0f, IM_COL32(255, 80, 80, 255));
        draw->AddText(ImVec2(P_screen.x + 6, P_screen.y - 8), IM_COL32(255, 80, 80, 255), "P");
    }
}

// ---------------------------------------------------------------------------
// RenderSpeedoWindow
// ---------------------------------------------------------------------------
void RenderSpeedoWindow()
{
    if (!ShowSpeedo) return;

    float speed    = SpeedoComputeSpeed();
    float maxSpeed = SpeedUnitMph ? MAX_SPEED_MPH : MAX_SPEED_KMH;
    float t        = std::fmin(speed / maxSpeed, 1.0f);

    // Needle spring physics
    static float s_needlePos = 0.0f;
    static float s_needleVel = 0.0f;
    {
        float dt_needle = ImGui::GetIO().DeltaTime;
        float force     = (t - s_needlePos) * SpeedoSpringK - s_needleVel * SpeedoDamping;
        s_needleVel    += force * dt_needle;
        s_needlePos    += s_needleVel * dt_needle;
        s_needlePos     = std::fmax(0.0f, std::fmin(s_needlePos, 1.0f));
        t               = s_needlePos;
    }

    const char* unitLabel = SpeedUnitMph ? "mph" : "km/h";

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
    // Geometry
    // -------------------------------------------------------------------------
    float arcAngleRad  = std::fmax(SpeedoArcAngle, 1.0f) * DEG_TO_RAD;
    arcAngleRad        = std::fmin(arcAngleRad, TWO_PI * 0.999f);
    float radius       = SpeedoArcLength / arcAngleRad;
    float maxPDist     = std::fmin(200.0f, radius);
    float pDist        = radius - std::fmin(SpeedoPDistance, maxPDist);
    float axisAngleRad = SpeedoAngle * DEG_TO_RAD;
    float arcMidAngle  = axisAngleRad;
    float arcStart     = arcMidAngle - arcAngleRad * 0.5f;
    float arcEnd       = arcMidAngle + arcAngleRad * 0.5f;
    float needleAngle  = arcStart + t * (arcEnd - arcStart);

    // P in local space (C at origin)
    ImVec2 P_local(
        std::cos(axisAngleRad) * pDist,
        std::sin(axisAngleRad) * pDist);

    // -------------------------------------------------------------------------
    // Bounding box (local space, C at origin)
    // -------------------------------------------------------------------------
    const float padding = 8.0f;
    float minX =  1e9f, minY =  1e9f;
    float maxX = -1e9f, maxY = -1e9f;

    for (int i = 0; i <= ARC_SEGMENTS; i++)
    {
        float a = arcStart + (arcEnd - arcStart) * static_cast<float>(i) / ARC_SEGMENTS;
        float x = std::cos(a) * radius;
        float y = std::sin(a) * radius;
        minX = std::min(minX, x); minY = std::min(minY, y);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y);
    }

    // Include P but not C
    minX = std::min(minX, P_local.x); minY = std::min(minY, P_local.y);
    maxX = std::max(maxX, P_local.x); maxY = std::max(maxY, P_local.y);

    float windowW = (maxX - minX) + padding * 2.0f;
    float windowH = (maxY - minY) + padding * 2.0f;
    float offsetX = -minX + padding;
    float offsetY = -minY + padding;

    ImGuiIO& io = ImGui::GetIO();

    // Default position if not yet set
    float defaultX = io.DisplaySize.x * 0.5f - windowW * 0.5f;
    float defaultY = io.DisplaySize.y - windowH - 200.0f;
    float wx       = SpeedoWindowX < 0.0f ? defaultX : SpeedoWindowX;
    float wy       = SpeedoWindowY < 0.0f ? defaultY : SpeedoWindowY;

    auto toScreen = [&](float lx, float ly) -> ImVec2 {
        return ImVec2(wx + lx + offsetX, wy + ly + offsetY);
    };

    // -------------------------------------------------------------------------
    // Edit mode — draw inside ImGui window so it can be dragged
    // -------------------------------------------------------------------------
    if (SpeedoEditMode)
    {
        ImGui::SetNextWindowPos(ImVec2(wx, wy), ImGuiCond_Once);
        ImGui::SetNextWindowSize(ImVec2(windowW, windowH), ImGuiCond_Always);
        ImGui::SetNextWindowBgAlpha(0.4f);
        ImGui::Begin("##speedo_tach", nullptr,
            ImGuiWindowFlags_NoDecoration       |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav              |
            ImGuiWindowFlags_NoScrollbar        |
            ImGuiWindowFlags_NoScrollWithMouse
        );

        // Save position when window is moved
        ImVec2 pos = ImGui::GetWindowPos();
        if (pos.x != SpeedoWindowX || pos.y != SpeedoWindowY)
        {
            SpeedoWindowX = pos.x;
            SpeedoWindowY = pos.y;
            SaveCurrentSettings();
        }

        // Recalculate toScreen using actual window position (may differ from wx/wy on first frame)
        ImVec2 wPos = ImGui::GetWindowPos();
        auto toScreenEdit = [&](float lx, float ly) -> ImVec2 {
            return ImVec2(wPos.x + lx + offsetX, wPos.y + ly + offsetY);
        };

        DrawSpeedoContent(ImGui::GetWindowDrawList(), toScreenEdit,
            t, speed, unitLabel, radius, pDist,
            axisAngleRad, arcStart, arcEnd, arcMidAngle, needleAngle, true);

        ImGui::End();
    }
    // -------------------------------------------------------------------------
    // Play mode — draw directly to background draw list, no ImGui window
    // -------------------------------------------------------------------------
    else
    {
        DrawSpeedoContent(ImGui::GetBackgroundDrawList(), toScreen,
            t, speed, unitLabel, radius, pDist,
            axisAngleRad, arcStart, arcEnd, arcMidAngle, needleAngle, false);
    }
}