// render_speedo.cpp
// Speedometer overlay for Split Wars 2.
//
// ─── Tachometer geometry ────────────────────────────────────────────────────
//
//   SpeedoArcAngle  — total sweep of the arc in degrees (1-359)
//   SpeedoArcLength — total length of the arc in pixels
//   SpeedoAngle     — rotation of the whole speedo (0-360)
//   SpeedoPDistance — distance of needle origin P from the arc (0=on arc, max 200px)
//
//   radius   = SpeedoArcLength / (SpeedoArcAngle * DEG_TO_RAD)
//   pDist    = radius - min(SpeedoPDistance, min(200, radius))
//   arcMid   = SpeedoAngle axis direction
//   arcStart = arcMid - ArcAngle/2
//   arcEnd   = arcMid + ArcAngle/2
//   needle   = line from P to arc point at current speed
//
// ─── Arc rendering ──────────────────────────────────────────────────────────
//
//   Up to 4 shared color+thickness stops along the arc (position 0-1).
//   Stop 1 always at position 0. Others added/removed by the user.
//   Between stops: smooth gradient (interpolated) or hard cut.
//   Alpha channel on stop colors controls fade.
//
// ─── Render modes ───────────────────────────────────────────────────────────
//
//   Edit mode — ImGui window, draggable, saves SpeedoWindowX/Y.
//   Play mode — draws to background draw list, no ImGui window occlusion.
//
// ────────────────────────────────────────────────────────────────────────────

#include "render_shared.h"
#include "render_speedo.h"
#include "stream_fonts.h"
#include <chrono>
#include <cmath>
#include <deque>
#include <algorithm>
#include <functional>

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr float MPS_TO_KMH    = 3.6f;
static constexpr float MPS_TO_MPH    = 2.23694f;
static constexpr float MAX_SPEED_KMH = 200.0f;
static constexpr float MAX_SPEED_MPH = MAX_SPEED_KMH / 1.60934f;
static constexpr float DEG_TO_RAD    = 3.14159265f / 180.0f;
static constexpr float PI            = 3.14159265f;
static constexpr float TWO_PI        = 6.28318530f;

// Segments scales with radius and thickness to avoid gaps in thick arcs
static int ArcSegments(float radius, float thickness)
{
    // At minimum: enough segments so adjacent endpoints overlap at given thickness
    // circumference / thickness gives a safe lower bound
    float circ = TWO_PI * radius;
    int   min  = (int)std::ceil(circ / std::fmax(thickness * 0.5f, 1.0f));
    return std::max(64, std::min(min, 512));
}

// ---------------------------------------------------------------------------
// Stop helpers
// ---------------------------------------------------------------------------
struct SpeedoStop
{
    float pos;
    float color[4];
    float thickness;
};

static int GatherStops(SpeedoStop out[4])
{
    int n = 0;
    out[n++] = { 0.0f,
        { SpeedoStop1Color[0], SpeedoStop1Color[1], SpeedoStop1Color[2], SpeedoStop1Color[3] },
        SpeedoStop1Thickness };
    if (SpeedoStop2Enabled)
        out[n++] = { SpeedoStop2Pos,
            { SpeedoStop2Color[0], SpeedoStop2Color[1], SpeedoStop2Color[2], SpeedoStop2Color[3] },
            SpeedoStop2Thickness };
    if (SpeedoStop3Enabled)
        out[n++] = { SpeedoStop3Pos,
            { SpeedoStop3Color[0], SpeedoStop3Color[1], SpeedoStop3Color[2], SpeedoStop3Color[3] },
            SpeedoStop3Thickness };
    if (SpeedoStop4Enabled)
        out[n++] = { SpeedoStop4Pos,
            { SpeedoStop4Color[0], SpeedoStop4Color[1], SpeedoStop4Color[2], SpeedoStop4Color[3] },
            SpeedoStop4Thickness };
    return n;
}

static void SampleStops(const SpeedoStop* stops, int n, float p, bool smooth,
                         float outColor[4], float& outThickness)
{
    if (n == 1)
    {
        for (int i = 0; i < 4; i++) outColor[i] = stops[0].color[i];
        outThickness = stops[0].thickness;
        return;
    }
    for (int i = 0; i < n - 1; i++)
    {
        if (p <= stops[i + 1].pos)
        {
            if (smooth)
            {
                float seg = stops[i + 1].pos - stops[i].pos;
                float t   = seg > 0.0f ? (p - stops[i].pos) / seg : 0.0f;
                for (int j = 0; j < 4; j++)
                    outColor[j] = stops[i].color[j] + (stops[i+1].color[j] - stops[i].color[j]) * t;
                outThickness = stops[i].thickness + (stops[i+1].thickness - stops[i].thickness) * t;
            }
            else
            {
                for (int j = 0; j < 4; j++) outColor[j] = stops[i].color[j];
                outThickness = stops[i].thickness;
            }
            return;
        }
    }
    for (int i = 0; i < 4; i++) outColor[i] = stops[n-1].color[i];
    outThickness = stops[n-1].thickness;
}

static ImU32 ColorToU32(const float c[4], float masterAlpha)
{
    return IM_COL32(
        (int)(c[0] * 255),
        (int)(c[1] * 255),
        (int)(c[2] * 255),
        (int)(c[3] * masterAlpha * 255));
}

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
// DrawArcSegmented
// ---------------------------------------------------------------------------
static void DrawArcSegmented(
    ImDrawList*       draw,
    ImVec2            center,
    float             radius,
    float             arcStart,
    float             arcEnd,
    float             arcFill,
    const SpeedoStop* stops,
    int               stopCount,
    bool              smooth,
    float             masterAlpha,
    bool              isBg)
{
    int   segs     = ArcSegments(radius, isBg ? SpeedoArcBgWidth : stops[stopCount-1].thickness);
    float arcRange = arcEnd - arcStart;
    float fillEnd  = arcStart + arcRange * arcFill;

    for (int i = 0; i < segs; i++)
    {
        float t0 = static_cast<float>(i)     / segs;
        float t1 = static_cast<float>(i + 1) / segs;
        float a0 = arcStart + arcRange * t0;
        float a1 = arcStart + arcRange * t1;

        if (a0 >= fillEnd) break;
        if (a1 >  fillEnd) a1 = fillEnd;

        ImVec2 p0(center.x + std::cos(a0) * radius, center.y + std::sin(a0) * radius);
        ImVec2 p1(center.x + std::cos(a1) * radius, center.y + std::sin(a1) * radius);

        float color[4]; float thickness;
        if (isBg)
        {
            color[0] = 0.31f; color[1] = 0.31f; color[2] = 0.31f; color[3] = 0.78f;
            thickness = SpeedoArcBgWidth;
        }
        else
        {
            float tmid = (t0 + t1) * 0.5f;
            SampleStops(stops, stopCount, tmid, smooth, color, thickness);
        }

        // Extend segment slightly to prevent gaps at high thickness
        ImVec2 dir(p1.x - p0.x, p1.y - p0.y);
        float  len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
        if (len > 0.0f)
        {
            float ext = thickness * 0.5f / len;
            p1.x += dir.x * ext;
            p1.y += dir.y * ext;
        }

        draw->AddLine(p0, p1, ColorToU32(color, masterAlpha), thickness);
    }
}

// ---------------------------------------------------------------------------
// DrawSpeedoContent
// ---------------------------------------------------------------------------
static void DrawSpeedoContent(
    ImDrawList*                          draw,
    std::function<ImVec2(float, float)>  toScreen,
    float                                t,
    float                                peakT,
    float                                speed,
    const char*                          unitLabel,
    float                                radius,
    float                                pDist,
    float                                axisAngleRad,
    float                                arcStart,
    float                                arcEnd,
    float                                arcMidAngle,
    float                                needleAngle,
    bool                                 straightLine, 
    bool                                 editMode)
{
    ImVec2 C_screen = toScreen(0.0f, 0.0f);
    ImVec2 P_screen = toScreen(
        std::cos(axisAngleRad) * pDist,
        std::sin(axisAngleRad) * pDist);
    
    SpeedoStop stops[4];
    int        stopCount = GatherStops(stops);

    if (straightLine)
    {
        float perpAngle = axisAngleRad + PI * 0.5f;
        float halfLen   = SpeedoArcLength * 0.5f;
    
        ImVec2 lineStart = toScreen( std::cos(perpAngle) * halfLen,  std::sin(perpAngle) * halfLen);
        ImVec2 lineEnd   = toScreen(-std::cos(perpAngle) * halfLen, -std::sin(perpAngle) * halfLen);
    
        draw->AddLine(lineStart, lineEnd,
            IM_COL32(80, 80, 80, (int)(200 * SpeedoOpacity)), SpeedoArcBgWidth);
    
        ImVec2 fillEnd = toScreen(
            std::cos(perpAngle) * halfLen * (1.0f - t * 2.0f),
            std::sin(perpAngle) * halfLen * (1.0f - t * 2.0f));
        draw->AddLine(lineEnd, fillEnd,
            ColorToU32(stops[0].color, SpeedoOpacity), stops[0].thickness);
    
        if (SpeedoNeedleVisible)
            draw->AddLine(P_screen, fillEnd,
                IM_COL32(255, 255, 255, (int)(230 * SpeedoOpacity)), SpeedoNeedleWidth);
    
        return; // skip arc drawing below
    }

    // Background arc
    DrawArcSegmented(draw, C_screen, radius, arcStart, arcEnd, 1.0f,
                     stops, stopCount, SpeedoGradientSmooth, SpeedoOpacity, true);

    // Decorative line
    if (SpeedoDecoLineEnabled)
    {
        float decoRadius = radius + SpeedoDecoLineOffset;
        if (decoRadius > 0.0f)
        {
            int segs = ArcSegments(decoRadius, 1.0f);
            draw->PathArcTo(C_screen, decoRadius, arcStart, arcEnd, segs);
            draw->PathStroke(
                IM_COL32(
                    (int)(SpeedoDecoLineColor[0] * 255),
                    (int)(SpeedoDecoLineColor[1] * 255),
                    (int)(SpeedoDecoLineColor[2] * 255),
                    (int)(SpeedoDecoLineColor[3] * SpeedoOpacity * 255)),
                false, 1.0f);
        }
    }

    // Filled sweep
    if (t > 0.0f)
        DrawArcSegmented(draw, C_screen, radius, arcStart, arcEnd, t,
                         stops, stopCount, SpeedoGradientSmooth, SpeedoOpacity, false);

    // Peak hold marker
    if (SpeedoPeakHoldEnabled && peakT > 0.0f)
    {
        float  peakAngle = arcStart + peakT * (arcEnd - arcStart);
        float  innerR    = radius - SpeedoTickHeight * 0.5f;
        float  outerR    = radius + SpeedoTickHeight * 0.5f;
        ImVec2 pk0(C_screen.x + std::cos(peakAngle) * innerR,
                   C_screen.y + std::sin(peakAngle) * innerR);
        ImVec2 pk1(C_screen.x + std::cos(peakAngle) * outerR,
                   C_screen.y + std::sin(peakAngle) * outerR);
        draw->AddLine(pk0, pk1,
            IM_COL32(255, 255, 255, (int)(200 * SpeedoOpacity)), 2.0f);
    }

    // Tick marks
    if (SpeedoTicksEnabled)
    {
        float maxSpeed = SpeedUnitMph ? MAX_SPEED_MPH : MAX_SPEED_KMH;
        float arcRange = arcEnd - arcStart;

        for (float spd = SpeedoTickInterval; spd < maxSpeed; spd += SpeedoTickInterval)
        {
            float tp         = spd / maxSpeed;
            float tickAngle  = arcStart + tp * arcRange;
            bool  isMajor    = std::fmod(spd, SpeedoTickMajorInterval) < 0.5f;
            float tickH      = isMajor ? SpeedoTickHeight : SpeedoTickHeight * 0.6f;
            float tickW      = isMajor ? 1.5f  : 1.5f  * 0.7f;
            float innerR     = radius - tickH * 0.5f;
            float outerR     = radius + tickH * 0.5f;

            ImVec2 ti(C_screen.x + std::cos(tickAngle) * innerR,
                      C_screen.y + std::sin(tickAngle) * innerR);
            ImVec2 to(C_screen.x + std::cos(tickAngle) * outerR,
                      C_screen.y + std::sin(tickAngle) * outerR);
            draw->AddLine(ti, to,
                IM_COL32(255, 255, 255, (int)(180 * SpeedoOpacity)), tickW);
        }
    }

    // Needle
    if (SpeedoNeedleVisible)
    {
        ImVec2 needleTip(
            C_screen.x + std::cos(needleAngle) * radius,
            C_screen.y + std::sin(needleAngle) * radius);
        draw->AddLine(P_screen, needleTip,
            IM_COL32(255, 255, 255, (int)(230 * SpeedoOpacity)),
            SpeedoNeedleWidth);
    }

    // Speed label
    if (SpeedoLabelVisible)
    {
        char    buf[16];
        snprintf(buf, sizeof(buf), "%.0f %s", speed, unitLabel);

        ImFont* font = GetStreamFont(SpeedoFontName, (float)SpeedoFontSize);

        ImVec2 apex = toScreen(
            std::cos(arcMidAngle) * radius,
            std::sin(arcMidAngle) * radius);

        ImU32 textCol = IM_COL32(255, 255, 255, (int)(200 * SpeedoOpacity));

        if (font)
        {
            ImVec2 textSize = font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, buf);
            draw->AddText(font, font->FontSize,
                ImVec2(apex.x - textSize.x * 0.5f, apex.y + 8.0f),
                textCol, buf);
        }
        else
        {
            ImVec2 textSize = ImGui::CalcTextSize(buf);
            draw->AddText(
                ImVec2(apex.x - textSize.x * 0.5f, apex.y + 8.0f),
                textCol, buf);
        }
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

    // Sanitize settings — guard against manual text entry or bad ini values
    SpeedoArcAngle          = std::fmax(SpeedoArcAngle,          0.0f);
    SpeedoArcLength         = std::fmax(SpeedoArcLength,         1.0f);
    SpeedoOpacity           = std::fmin(std::fmax(SpeedoOpacity,  0.0f),  1.0f);
    SpeedoNeedleWidth       = std::fmax(SpeedoNeedleWidth,       0.1f);
    SpeedoArcBgWidth        = std::fmax(SpeedoArcBgWidth,        0.1f);
    SpeedoStop1Thickness    = std::fmax(SpeedoStop1Thickness,    0.1f);
    SpeedoStop2Thickness    = std::fmax(SpeedoStop2Thickness,    0.1f);
    SpeedoStop3Thickness    = std::fmax(SpeedoStop3Thickness,    0.1f);
    SpeedoStop4Thickness    = std::fmax(SpeedoStop4Thickness,    0.1f);
    SpeedoStop2Pos          = std::fmin(std::fmax(SpeedoStop2Pos, 0.01f), 1.0f);
    SpeedoStop3Pos          = std::fmin(std::fmax(SpeedoStop3Pos, 0.01f), 1.0f);
    SpeedoStop4Pos          = std::fmin(std::fmax(SpeedoStop4Pos, 0.01f), 1.0f);
    SpeedoTickInterval      = std::fmax(SpeedoTickInterval,      1.0f);
    SpeedoTickMajorInterval = std::fmax(SpeedoTickMajorInterval, 1.0f);
    SpeedoTickHeight        = std::fmax(SpeedoTickHeight,        1.0f);
    SpeedoPeakHoldTime      = std::fmax(SpeedoPeakHoldTime,      0.1f);
    SpeedoSpringK           = std::fmax(SpeedoSpringK,           0.1f);
    SpeedoDamping           = std::fmax(SpeedoDamping,           0.1f);
    SpeedoFontSize          = std::fmin(std::fmax(SpeedoFontSize, 16.0f), 48.0f);

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

    // Peak hold
    static float s_peakT     = 0.0f;
    static float s_peakDecay = 0.0f;
    if (SpeedoPeakHoldEnabled)
    {
        if (t > s_peakT)
        {
            s_peakT     = t;
            s_peakDecay = SpeedoPeakHoldTime;
        }
        else
        {
            s_peakDecay -= ImGui::GetIO().DeltaTime;
            if (s_peakDecay <= 0.0f)
                s_peakT = std::fmax(s_peakT - ImGui::GetIO().DeltaTime * 0.2f, 0.0f);
        }
    }
    else
    {
        s_peakT = 0.0f;
    }

    const char* unitLabel = SpeedUnitMph ? "mph" : "km/h";

    // Numeric mode
    if (!SpeedoTachometer)
    {
        ImGui::SetNextWindowPos(ImVec2(10, 200), ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowBgAlpha(0.6f);
        ImGui::Begin("##speedo", nullptr,
            ImGuiWindowFlags_NoDecoration       |
            ImGuiWindowFlags_AlwaysAutoResize   |
            ImGuiWindowFlags_NoFocusOnAppearing |
            ImGuiWindowFlags_NoNav);
        ImGui::Text("%.0f %s", speed, unitLabel);
        ImGui::End();
        return;
    }

    // Geometry
    static constexpr float STRAIGHT_LINE_THRESHOLD = 1.0f;
    bool  straightLine = SpeedoArcAngle < STRAIGHT_LINE_THRESHOLD;
    float axisAngleRad = SpeedoAngle * DEG_TO_RAD;
    float arcAngleRad  = 0.0f;
    float radius       = 0.0f;
    float pDist        = 0.0f;
    float arcMidAngle  = axisAngleRad;
    float arcStart     = 0.0f;
    float arcEnd       = 0.0f;
    float needleAngle  = 0.0f;
    
    if (!straightLine)
    {
        arcAngleRad = std::fmin(SpeedoArcAngle * DEG_TO_RAD, TWO_PI * 0.999f);
        radius      = SpeedoArcLength / arcAngleRad;
        float maxPDist = std::fmin(200.0f, radius);
        pDist       = radius - std::fmin(SpeedoPDistance, maxPDist);
        arcStart    = axisAngleRad - arcAngleRad * 0.5f;
        arcEnd      = axisAngleRad + arcAngleRad * 0.5f;
        needleAngle = arcStart + t * (arcEnd - arcStart);
    }

    ImVec2 P_local(
        std::cos(axisAngleRad) * pDist,
        std::sin(axisAngleRad) * pDist);

    // Bounding box — include deco line and tick overhang
    const float padding    = 8.0f;
    float       bboxExtra  = SpeedoTicksEnabled ? SpeedoTickHeight * 0.5f + 2.0f : 0.0f;
    if (SpeedoDecoLineEnabled)
        bboxExtra = std::fmax(bboxExtra, std::fabs(SpeedoDecoLineOffset) + 2.0f);
    float bboxRadius = radius + bboxExtra;

    float minX =  1e9f, minY =  1e9f;
    float maxX = -1e9f, maxY = -1e9f;
    int   bboxSegs = ArcSegments(bboxRadius, 1.0f);
    for (int i = 0; i <= bboxSegs; i++)
    {
        float a = arcStart + (arcEnd - arcStart) * static_cast<float>(i) / bboxSegs;
        float x = std::cos(a) * bboxRadius;
        float y = std::sin(a) * bboxRadius;
        minX = std::min(minX, x); minY = std::min(minY, y);
        maxX = std::max(maxX, x); maxY = std::max(maxY, y);
    }
    minX = std::min(minX, P_local.x); minY = std::min(minY, P_local.y);
    maxX = std::max(maxX, P_local.x); maxY = std::max(maxY, P_local.y);

    float windowW = (maxX - minX) + padding * 2.0f;
    float windowH = (maxY - minY) + padding * 2.0f;
    float offsetX = -minX + padding;
    float offsetY = -minY + padding;

    ImGuiIO& io = ImGui::GetIO();
    float    wx = SpeedoWindowX < 0.0f ? io.DisplaySize.x * 0.5f - windowW * 0.5f : SpeedoWindowX;
    float    wy = SpeedoWindowY < 0.0f ? io.DisplaySize.y - windowH - 200.0f      : SpeedoWindowY;

    auto toScreen = [&](float lx, float ly) -> ImVec2 {
        return ImVec2(wx + lx + offsetX, wy + ly + offsetY);
    };

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
            ImGuiWindowFlags_NoScrollWithMouse);

        ImVec2 pos = ImGui::GetWindowPos();
        if (pos.x != SpeedoWindowX || pos.y != SpeedoWindowY)
        {
            SpeedoWindowX = pos.x;
            SpeedoWindowY = pos.y;
            SaveCurrentSettings();
        }

        ImVec2 wPos = ImGui::GetWindowPos();
        auto toScreenEdit = [&](float lx, float ly) -> ImVec2 {
            return ImVec2(wPos.x + lx + offsetX, wPos.y + ly + offsetY);
        };

        DrawSpeedoContent(ImGui::GetWindowDrawList(), toScreenEdit,
            t, s_peakT, speed, unitLabel, radius, pDist,
            axisAngleRad, arcStart, arcEnd, arcMidAngle, needleAngle, straightLine, true);

        ImGui::End();
    }
    else
    {
        DrawSpeedoContent(ImGui::GetBackgroundDrawList(), toScreen,
            t, s_peakT, speed, unitLabel, radius, pDist,
            axisAngleRad, arcStart, arcEnd, arcMidAngle, needleAngle, straightLine, false);
    }
}