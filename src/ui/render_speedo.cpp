// render_speedo.cpp
// Speedometer overlay for Split Wars 2.
//
// ─── Tachometer geometry ────────────────────────────────────────────────────
//
//   SpeedoArcAngle    — total sweep of the arc in degrees (1-359), 0 = straight line
//   SpeedoArcLength   — total length of the arc/line in pixels
//   SpeedoArcRotation — rotation of the whole speedo (0-360)
//   SpeedoPDistance   — distance of needle origin P from the arc (0=on arc, max 500px)
//
//   Arc mode:
//     radius   = SpeedoArcLength / (SpeedoArcAngle * DEG_TO_RAD)
//     pDist    = radius - min(SpeedoPDistance, min(500, radius))
//     arcMid   = SpeedoArcRotation axis direction
//     arcStart = arcMid - ArcAngle/2
//     arcEnd   = arcMid + ArcAngle/2
//     needle   = line from P to arc point at current speed
//
//   Straight line mode (SpeedoArcAngle < 1):
//     line runs perpendicular to SpeedoArcRotation, length = SpeedoArcLength
//     fill grows from lineStart toward lineEnd as speed increases
//
// ─── Render modes ───────────────────────────────────────────────────────────
//
//   Edit mode — ImGui window, draggable, saves SpeedoWindowX/Y.
//   Play mode — draws to background draw list, no ImGui window occlusion.
//
// ────────────────────────────────────────────────────────────────────────────

#include "render_shared.h"
#include "stream_fonts.h"
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>

namespace fs = std::filesystem;

// ---------------------------------------------------------------------------
// Texture state — loaded on demand when filenames change, never freed manually
// (Nexus owns the lifetime).
// ---------------------------------------------------------------------------
static Texture_t* s_faceTexture         = nullptr;
static std::string s_loadedFacePath;
static Texture_t* s_needleTexture       = nullptr;
static std::string s_loadedNeedleTexPath;

// Cached list of PNG/JPG filenames found in the textures folder.
static std::vector<std::string> s_textureNames;
static bool                     s_textureNamesScanned = false;

// Scan (or re-scan) the textures directory and rebuild s_textureNames.
void ScanTextureFiles()
{
    s_textureNames.clear();
    s_textureNamesScanned = true;

    std::string texDir = GetAddonDir() + "\\textures";

    std::error_code ec;
    fs::create_directories(texDir, ec);
    for (auto& entry : fs::directory_iterator(texDir, ec))
    {
        if (!entry.is_regular_file(ec)) continue;
        auto ext = entry.path().extension().string();
        for (auto& c : ext) c = (char)std::tolower((unsigned char)c);
        if (ext == ".png" || ext == ".jpg" || ext == ".jpeg")
            s_textureNames.push_back(entry.path().filename().string());
    }
    std::sort(s_textureNames.begin(), s_textureNames.end());
}

const std::vector<std::string>& GetSpeedoTextureNames()
{
    if (!s_textureNamesScanned)
        ScanTextureFiles();
    return s_textureNames;
}

// Returns the currently loaded needle texture's native pixel size, or
// (0,0) if no needle texture is loaded. Used by the options panel to
// show the image-centre default when SpeedoNeedleTexPivotX/Y == -1.
bool GetSpeedoNeedleTexSize(float& outW, float& outH)
{
    if (!s_needleTexture || !s_needleTexture->Resource)
    {
        outW = outH = 0.0f;
        return false;
    }
    outW = (float)s_needleTexture->Width;
    outH = (float)s_needleTexture->Height;
    return true;
}

// Reload textures when filenames change.
// Files are resolved relative to <GW2>/addons/Split Wars 2/textures/.
//
// Uses Textures_LoadFromFile (async, callback-driven) rather than
// Textures_GetOrCreateFromFile. The latter can return a Texture_t* whose
// Resource is still null while the file decodes/uploads in the background,
// with no signal telling the caller when it becomes ready — so a naive
// "mark this path as loaded, never ask again" cache (which is what this
// function used to do) could get stuck showing nothing until something
// cleared the cache and re-triggered a fresh request. LoadFromFile instead
// calls us back exactly once, with the fully-ready texture, whenever it's
// actually done.
static void OnSpeedoTextureReceived(const char* aIdentifier, Texture_t* aTexture)
{
    if (std::strcmp(aIdentifier, "SW2_SPEEDO_FACE") == 0)
        s_faceTexture = aTexture;
    else if (std::strcmp(aIdentifier, "SW2_SPEEDO_NEEDLE") == 0)
        s_needleTexture = aTexture;
}

static void UpdateSpeedoTextures()
{
    // Build base path once — APIDefs->Paths_GetAddonDirectory returns a
    // persistent string so it's safe to call every frame.
    std::string texDir = std::string(APIDefs->Paths_GetAddonDirectory("Split Wars 2"))
                         + "\\textures\\";

    if (SpeedoFaceEnabled && !SpeedoFacePath.empty() &&
        SpeedoFacePath != s_loadedFacePath)
    {
        std::string fullPath = texDir + SpeedoFacePath;
        s_faceTexture    = nullptr; // hide old/mismatched texture while the new one loads
        s_loadedFacePath = SpeedoFacePath;
        APIDefs->Textures_LoadFromFile("SW2_SPEEDO_FACE", fullPath.c_str(), OnSpeedoTextureReceived);
    }
    if (!SpeedoFaceEnabled)
    {
        s_faceTexture    = nullptr;
        s_loadedFacePath.clear();
    }

    if (SpeedoNeedleTexEnabled && !SpeedoNeedleTexPath.empty() &&
        SpeedoNeedleTexPath != s_loadedNeedleTexPath)
    {
        std::string fullPath = texDir + SpeedoNeedleTexPath;
        s_needleTexture       = nullptr; // hide old/mismatched texture while the new one loads
        s_loadedNeedleTexPath = SpeedoNeedleTexPath;
        APIDefs->Textures_LoadFromFile("SW2_SPEEDO_NEEDLE", fullPath.c_str(), OnSpeedoTextureReceived);
    }
    if (!SpeedoNeedleTexEnabled)
    {
        s_needleTexture       = nullptr;
        s_loadedNeedleTexPath.clear();
    }
}

// Draw a texture so that its local pivot point (px,py), in unscaled
// source-image pixels from the top-left corner, lands at screen position
// (cx,cy), scaled by `scale` and rotated by angleRad about that pivot.
// The texture is drawn as a quad with four rotated corners.
static void DrawRotatedImage(
    ImDrawList* draw,
    ImTextureID texID,
    float       cx, float cy,
    float       texW, float texH,
    float       scale,
    float       px, float py,
    float       angleRad)
{
    float cosA = std::cos(angleRad);
    float sinA = std::sin(angleRad);

    // Corner positions relative to the pivot, in scaled pixels.
    float left   = -px * scale;
    float top    = -py * scale;
    float right  = (texW - px) * scale;
    float bottom = (texH - py) * scale;

    // Corners relative to pivot, then rotated and placed at (cx,cy).
    auto rot = [&](float lx, float ly) -> ImVec2 {
        return ImVec2(cx + lx * cosA - ly * sinA,
                      cy + lx * sinA + ly * cosA);
    };

    ImVec2 tl = rot(left,  top);
    ImVec2 tr = rot(right, top);
    ImVec2 br = rot(right, bottom);
    ImVec2 bl = rot(left,  bottom);

    draw->AddImageQuad(texID,
        tl, tr, br, bl,
        ImVec2(0,0), ImVec2(1,0), ImVec2(1,1), ImVec2(0,1),
        IM_COL32_WHITE);
}

// ---------------------------------------------------------------------------
// Constants
// ---------------------------------------------------------------------------
static constexpr float MPS_TO_KMH              = 3.6f;
static constexpr float MPS_TO_MPH              = 2.23694f;
static constexpr float MPS_TO_UPS              = 1.0f / 0.0254f; // GW2: 1 unit = 1 inch
static constexpr float MAX_SPEED_KMH           = 220.0f;
static constexpr float MAX_SPEED_MPH           = MAX_SPEED_KMH / 1.60934f;
static constexpr float MAX_SPEED_UPS           = MAX_SPEED_KMH / 3.6f * MPS_TO_UPS;
static constexpr float DEG_TO_RAD              = 3.14159265f / 180.0f;
static constexpr float PI                      = 3.14159265f;
static constexpr float TWO_PI                  = 6.28318530f;
static constexpr float STRAIGHT_LINE_THRESHOLD = 1.0f;

// Picks a segment count for AddLine-based arc/line drawing. Finer
// thickness needs more segments to keep the polyline looking curved
// rather than faceted, so segment count scales inversely with thickness
// (half-thickness is used as a rough "pixels per segment" budget).
// Clamped to [64, 512]: 64 keeps thin/short arcs cheap, 512 caps cost on
// huge radii or very thin strokes where the raw formula would otherwise
// explode.
static int ArcSegments(float radius, float thickness)
{
    float circ = TWO_PI * radius;
    int   n    = (int)std::ceil(circ / std::fmax(thickness * 0.5f, 1.0f));
    return std::max(64, std::min(n, 512));
}

static int LineSegments(float length, float thickness)
{
    int n = (int)std::ceil(length / std::fmax(thickness * 0.5f, 1.0f));
    return std::max(64, std::min(n, 512));
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

// Returns the color/thickness for gradient position p in [0,1], given the
// active stops (stops[0] is always at pos 0). Before the first interior
// stop or with only one stop active, returns stop 0 unchanged. Between two
// stops, either linearly blends (smooth=true) or hard-steps to the next
// stop's value (smooth=false). Past the last stop, clamps to that stop's
// value.
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
// DrawArcSegmented
// Draws arc with per-segment interpolated color+thickness.
// arcFill: 0-1 how much of the arc to draw.
// isBg: use background color/width instead of stops.
// wholeArc: when true, every segment's COLOR is sampled at arcFill itself
// (the current speed), so the whole arc is one uniform color that fades/
// steps together as speed changes, rather than each segment showing the
// color for its own position. Thickness is unaffected and still varies
// per-segment by position, same as the position-gradient mode.
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
    bool              wholeArc,
    float             masterAlpha,
    bool              isBg)
{
    int   segs     = ArcSegments(radius, isBg ? SpeedoArcBgWidth : stops[stopCount-1].thickness);
    float arcRange = arcEnd - arcStart;
    float fillEnd  = arcStart + arcRange * arcFill;

    // In whole-arc mode every segment uses this same color all the way
    // through, so it only needs to be sampled once per draw call rather
    // than per segment.
    float wholeArcColor[4]; float wholeArcDummyThickness;
    if (!isBg && wholeArc)
        SampleStops(stops, stopCount, arcFill, smooth, wholeArcColor, wholeArcDummyThickness);

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
            // Thickness uses the max of both endpoint samples (not the midpoint)
            // so a segment never draws thinner than either side of a thickness
            // step — that would otherwise leave a visible notch where the
            // stop's thickness changes. This still samples by position even
            // in whole-arc mode, since thickness is unaffected by that flag.
            float color0[4]; float thickness0;
            float color1[4]; float thickness1;
            SampleStops(stops, stopCount, t0, smooth, color0, thickness0);
            SampleStops(stops, stopCount, t1, smooth, color1, thickness1);
            thickness = std::fmax(thickness0, thickness1);

            if (wholeArc)
            {
                // Color is sampled once at arcFill itself.
                for (int j = 0; j < 4; j++) color[j] = wholeArcColor[j];
            }
            else
            {
                // Color is sampled at the segment midpoint so a hard color step
                // lands mid-segment rather than at an edge.
                float dummyThickness;
                SampleStops(stops, stopCount, (t0 + t1) * 0.5f, smooth, color, dummyThickness);
            }
        }

        // Extend slightly to prevent gaps
        ImVec2 dir(p1.x - p0.x, p1.y - p0.y);
        float  len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
        if (len > 0.0f)
        {
            float ext = thickness * 0.5f / len;
            p0.x -= dir.x * ext;
            p0.y -= dir.y * ext;
            p1.x += dir.x * ext;
            p1.y += dir.y * ext;
        }

        draw->AddLine(p0, p1, ColorToU32(color, masterAlpha), thickness);
    }
}


// ---------------------------------------------------------------------------
// DrawLineSegmented
// Mirrors DrawArcSegmented but for a straight line.
// linePoint(t) converts 0-1 position to screen coords.
// lineFill: 0-1 how much of the line to draw.
// isBg: use background color/width instead of stops.
// wholeArc: see DrawArcSegmented — uniform color sampled at lineFill itself.
// ---------------------------------------------------------------------------
static void DrawLineSegmented(
    ImDrawList*                         draw,
    std::function<ImVec2(float)>        linePoint,
    float                               length,
    float                               lineFill,
    const SpeedoStop*                   stops,
    int                                 stopCount,
    bool                                smooth,
    bool                                wholeArc,
    float                               masterAlpha,
    bool                                isBg)
{
    int segs = LineSegments(length, isBg ? SpeedoArcBgWidth : stops[stopCount-1].thickness);

    float wholeArcColor[4]; float wholeArcDummyThickness;
    if (!isBg && wholeArc)
        SampleStops(stops, stopCount, lineFill, smooth, wholeArcColor, wholeArcDummyThickness);

    for (int i = 0; i < segs; i++)
    {
        float t0 = static_cast<float>(i)     / segs;
        float t1 = static_cast<float>(i + 1) / segs;

        if (t0 >= lineFill) break;
        if (t1 >  lineFill) t1 = lineFill;

        float color[4]; float thickness;
        if (isBg)
        {
            color[0] = 0.31f; color[1] = 0.31f; color[2] = 0.31f; color[3] = 0.78f;
            thickness = SpeedoArcBgWidth;
        }
        else
        {
            // use max thickness of endpoints to prevent gaps, same in both modes
            float color0[4]; float thickness0;
            float color1[4]; float thickness1;
            SampleStops(stops, stopCount, t0, smooth, color0, thickness0);
            SampleStops(stops, stopCount, t1, smooth, color1, thickness1);
            thickness = std::fmax(thickness0, thickness1);

            if (wholeArc)
            {
                for (int j = 0; j < 4; j++) color[j] = wholeArcColor[j];
            }
            else
            {
                // re-sample mid for color only
                float dummy; SampleStops(stops, stopCount, (t0 + t1) * 0.5f, smooth, color, dummy);
            }
        }

        ImVec2 p0 = linePoint(t0);
        ImVec2 p1 = linePoint(t1);

        // Extend slightly to prevent gaps
        ImVec2 dir(p1.x - p0.x, p1.y - p0.y);
        float  len = std::sqrt(dir.x*dir.x + dir.y*dir.y);
        if (len > 0.0f)
        {
            float ext = thickness * 0.5f / len;
            p0.x -= dir.x * ext;
            p0.y -= dir.y * ext;
            p1.x += dir.x * ext;
            p1.y += dir.y * ext;
        }

        draw->AddLine(p0, p1, ColorToU32(color, masterAlpha), thickness);
    }
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
        float factor  = (SpeedUnit == 1) ? MPS_TO_MPH
                      : (SpeedUnit == 2) ? MPS_TO_UPS
                      : MPS_TO_KMH;
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

// Format "<speed> <unit>", or just "<speed>" when unitLabel is empty (no
// trailing space left behind).
static void FormatSpeedLabel(char* buf, size_t bufSize, float speed, const char* unitLabel)
{
    if (unitLabel && unitLabel[0] != '\0')
        snprintf(buf, bufSize, "%.0f %s", speed, unitLabel);
    else
        snprintf(buf, bufSize, "%.0f", speed);
}

// ---------------------------------------------------------------------------
// DrawSpeedLabel
// ---------------------------------------------------------------------------
static void DrawSpeedLabel(ImDrawList* draw, float speed,
                            const char* unitLabel, float masterAlpha)
{
    char    buf[16];
    FormatSpeedLabel(buf, sizeof(buf), speed, unitLabel);
    ImFont* font    = GetStreamFont(SpeedoFontName, (float)SpeedoFontSize);
    ImU32   textCol = IM_COL32(255, 255, 255, (int)(200 * masterAlpha));

    ImVec2 textSize = font
        ? font->CalcTextSizeA(font->FontSize, FLT_MAX, 0.0f, buf)
        : ImGui::CalcTextSize(buf);

    // SpeedoLabelX/Y anchor the label's right edge (vertically centred), so
    // the text grows leftward as the number gains digits instead of
    // expanding symmetrically around a centre point.
    ImVec2 drawPos(
        SpeedoLabelX - textSize.x,
        SpeedoLabelY - textSize.y * 0.5f);

    if (font)
        draw->AddText(font, font->FontSize, drawPos, textCol, buf);
    else
        draw->AddText(drawPos, textCol, buf);
}

// ---------------------------------------------------------------------------
// DrawEditMarkers
// ---------------------------------------------------------------------------
static void DrawEditMarkers(ImDrawList* draw, ImVec2 C_screen, ImVec2 P_screen)
{
    draw->AddCircleFilled(C_screen, 4.0f, IM_COL32(255, 200, 0, 255));
    draw->AddText(ImVec2(C_screen.x + 6, C_screen.y - 8), IM_COL32(255, 200, 0, 255), "C");
    draw->AddCircleFilled(P_screen, 4.0f, IM_COL32(255, 80, 80, 255));
    draw->AddText(ImVec2(P_screen.x + 6, P_screen.y - 8), IM_COL32(255, 80, 80, 255), "P");
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

    // =========================================================================
    // FACE TEXTURE — drawn via GetBackgroundDrawList() in RenderSpeedoWindow,
    // not here, so it is never clipped by the speedo window bounds.
    // =========================================================================

    SpeedoStop stops[4];
    int        stopCount = GatherStops(stops);

    // =========================================================================
    // STRAIGHT LINE FORK
    // =========================================================================
    if (straightLine)
    {
        float perpAngle = axisAngleRad + PI * 0.5f;
        float halfLen   = SpeedoArcLength * 0.5f;
        float cosPerp   = std::cos(perpAngle);
        float sinPerp   = std::sin(perpAngle);
        float cosAxis   = std::cos(axisAngleRad);
        float sinAxis   = std::sin(axisAngleRad);

        // 0 = lineStart, 1 = lineEnd
        auto linePoint = [&](float p) -> ImVec2 {
            return toScreen(
                cosPerp * halfLen * (p * 2.0f - 1.0f),
                sinPerp * halfLen * (p * 2.0f - 1.0f));
        };

        // --- Background line --- (SpeedoArcBgOpacity governs only this background element)
        DrawLineSegmented(draw, linePoint, SpeedoArcLength, 1.0f,
                          stops, stopCount, SpeedoGradientSmooth, SpeedoGradientWholeArc, SpeedoArcBgOpacity, true);

        // --- Filled sweep --- (full opacity; per-stop alpha still applies)
        if (t > 0.0f)
            DrawLineSegmented(draw, linePoint, SpeedoArcLength, t,
                              stops, stopCount, SpeedoGradientSmooth, SpeedoGradientWholeArc, 1.0f, false);

        // --- Peak hold marker ---
        if (SpeedoPeakHoldEnabled && peakT > 0.0f)
        {
            ImVec2 peakCenter = linePoint(peakT);
            float  tickH      = SpeedoPeakHoldSize * 0.5f;
            ImVec2 pk0(peakCenter.x + cosAxis * tickH, peakCenter.y + sinAxis * tickH);
            ImVec2 pk1(peakCenter.x - cosAxis * tickH, peakCenter.y - sinAxis * tickH);
            draw->AddLine(pk0, pk1,
                IM_COL32(255, 255, 255, 200), 2.0f);
        }

        // --- Edit mode markers ---
        if (editMode)
            DrawEditMarkers(draw, C_screen, P_screen);

        return;
    }

    // =========================================================================
    // ARC FORK
    // =========================================================================

    // --- Background arc --- (SpeedoArcBgOpacity governs only this background element)
    DrawArcSegmented(draw, C_screen, radius, arcStart, arcEnd, 1.0f,
                     stops, stopCount, SpeedoGradientSmooth, SpeedoGradientWholeArc, SpeedoArcBgOpacity, true);

    // --- Filled sweep arc --- (full opacity; per-stop alpha still applies)
    if (t > 0.0f)
        DrawArcSegmented(draw, C_screen, radius, arcStart, arcEnd, t,
                         stops, stopCount, SpeedoGradientSmooth, SpeedoGradientWholeArc, 1.0f, false);

    // --- Peak hold marker ---
    if (SpeedoPeakHoldEnabled && peakT > 0.0f)
    {
        float  peakAngle = arcStart + peakT * (arcEnd - arcStart);
        float  innerR    = radius - SpeedoPeakHoldSize * 0.5f;
        float  outerR    = radius + SpeedoPeakHoldSize * 0.5f;
        ImVec2 pk0(C_screen.x + std::cos(peakAngle) * innerR,
                   C_screen.y + std::sin(peakAngle) * innerR);
        ImVec2 pk1(C_screen.x + std::cos(peakAngle) * outerR,
                   C_screen.y + std::sin(peakAngle) * outerR);
        draw->AddLine(pk0, pk1,
            IM_COL32(255, 255, 255, 200), 2.0f);
    }

    // --- Needle ---
    // needleTip is the point on the arc (radius from C) that the needle
    // points at. The needle is drawn from P to this point, so its actual
    // on-screen direction is atan2(tip - P), not needleAngle itself —
    // those two only coincide when P == C.
    ImVec2 needleTip(
        C_screen.x + std::cos(needleAngle) * radius,
        C_screen.y + std::sin(needleAngle) * radius);

    if (SpeedoNeedleVisible)
    {
        draw->AddLine(P_screen, needleTip,
            IM_COL32(255, 255, 255, 230), SpeedoNeedleWidth);
    }

    // --- Needle texture ---
    if (SpeedoNeedleTexEnabled && s_needleTexture && s_needleTexture->Resource)
    {
        float texW = (float)s_needleTexture->Width;
        float texH = (float)s_needleTexture->Height;

        // Pivot point inside the texture (source pixels, top-left origin).
        // -1,-1 is the sentinel for "use the image centre".
        float px = (SpeedoNeedleTexPivotX < 0.0f) ? texW * 0.5f : SpeedoNeedleTexPivotX;
        float py = (SpeedoNeedleTexPivotY < 0.0f) ? texH * 0.5f : SpeedoNeedleTexPivotY;

        // Rotate by the needle's true visual direction (P -> tip), not by
        // needleAngle directly, so the texture tracks the drawn line
        // exactly even when P != C.
        float needleVisualAngle = std::atan2(needleTip.y - P_screen.y,
                                              needleTip.x - P_screen.x);

        // The texture's pivot is placed exactly on P_screen and rotated
        // about that point — correct at every needle angle, regardless of
        // where the pivot sits inside the image.
        DrawRotatedImage(draw, (ImTextureID)s_needleTexture->Resource,
                         P_screen.x, P_screen.y,
                         texW, texH, SpeedoNeedleTexScale,
                         px, py,
                         needleVisualAngle + SpeedoNeedleTexAngleOffset * DEG_TO_RAD);
    }

    // --- Edit mode markers ---
    if (editMode)
        DrawEditMarkers(draw, C_screen, P_screen);
}

// ---------------------------------------------------------------------------
// RenderSpeedoWindow
// ---------------------------------------------------------------------------
void RenderSpeedoWindow()
{
    if (!ShowSpeedo) return;
    if (!MumbleLink) return;
    if (MumbleLink->UITick == 0) return;

    UpdateSpeedoTextures();

    // Mount visibility filter
    if (SpeedoMountMask != -1)
    {
        int mountBit = 1 << (int)MumbleLink->Context.MountIndex;
        if (!(SpeedoMountMask & mountBit)) return;
    }

    // Sanitize settings
    SpeedoArcAngle          = std::fmax(SpeedoArcAngle,          0.0f);
    SpeedoArcLength         = std::fmax(SpeedoArcLength,         1.0f);
    SpeedoArcBgOpacity           = std::fmin(std::fmax(SpeedoArcBgOpacity,  0.0f), 1.0f);
    SpeedoNeedleWidth       = std::fmax(SpeedoNeedleWidth,       0.1f);
    SpeedoArcBgWidth        = std::fmax(SpeedoArcBgWidth,        0.1f);
    SpeedoStop1Thickness    = std::fmax(SpeedoStop1Thickness,    0.1f);
    SpeedoStop2Thickness    = std::fmax(SpeedoStop2Thickness,    0.1f);
    SpeedoStop3Thickness    = std::fmax(SpeedoStop3Thickness,    0.1f);
    SpeedoStop4Thickness    = std::fmax(SpeedoStop4Thickness,    0.1f);
    SpeedoStop2Pos          = std::fmin(std::fmax(SpeedoStop2Pos, 0.01f), 1.0f);
    SpeedoStop3Pos          = std::fmin(std::fmax(SpeedoStop3Pos, 0.01f), 1.0f);
    SpeedoStop4Pos          = std::fmin(std::fmax(SpeedoStop4Pos, 0.01f), 1.0f);
    SpeedoPeakHoldTime      = std::fmax(SpeedoPeakHoldTime,      0.1f);
    SpeedoSpringK           = std::fmax(SpeedoSpringK,           0.1f);
    SpeedoDamping           = std::fmax(SpeedoDamping,           0.1f);
    SpeedoFontSize          = std::fmin(std::fmax(SpeedoFontSize, 16.0f), 48.0f);

    float speed    = SpeedoComputeSpeed();
    float maxSpeed = (SpeedUnit == 1) ? MAX_SPEED_MPH : (SpeedUnit == 2) ? MAX_SPEED_UPS : MAX_SPEED_KMH;
    float t        = std::fmin(speed / maxSpeed, 1.0f);

    // Critically-damped spring-damper: s_needlePos chases target `t` instead
    // of snapping to it instantly, so the needle eases and slightly overshoots
    // like a real gauge. SpeedoSpringK is the spring stiffness (higher = faster
    // pull toward target), SpeedoDamping resists velocity (higher = less
    // overshoot/oscillation). `t` is reassigned to the smoothed s_needlePos
    // afterward so every consumer below (arc fill, needle angle, label) draws
    // the eased value, not the raw instantaneous speed ratio.
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

    const char* unitLabel = !SpeedoShowUnit ? ""
                          : (SpeedUnit == 1) ? "mph"
                          : (SpeedUnit == 2) ? "u/s"
                          : "km/h";
    maxSpeed = (SpeedUnit == 1) ? MAX_SPEED_MPH
                   : (SpeedUnit == 2) ? MAX_SPEED_UPS
                   : MAX_SPEED_KMH;

    // Speed label — independent window, always on foreground draw list
    if (SpeedoLabelVisible)
    {
        DrawSpeedLabel(ImGui::GetForegroundDrawList(), speed, unitLabel, 1.0f);

        if (SpeedoEditMode)
        {
            // Fixed-size drag handle, independent of the label text's
            // actual rendered width. The real label is drawn separately by
            // DrawSpeedLabel above, anchored at SpeedoLabelX/Y — this window
            // only exists to give the user something to grab and drag.
            // (A size derived from the live text used to jitter by a pixel
            // or two every frame as the digits changed, which fed into the
            // right-edge anchor correction below and made the window creep
            // sideways on its own. A fixed size has no such feedback loop.)
            const float lw = 200.0f;
            const float lh = 60.0f;

            ImGui::SetNextWindowPos(ImVec2(SpeedoLabelX - lw, SpeedoLabelY - lh * 0.5f), ImGuiCond_Once);
            ImGui::SetNextWindowSize(ImVec2(lw, lh), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.3f);
            ImGui::Begin("##speedo_label_drag", nullptr,
                ImGuiWindowFlags_NoDecoration       |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav              |
                ImGuiWindowFlags_NoScrollbar        |
                ImGuiWindowFlags_NoScrollWithMouse  |
                ImGuiWindowFlags_NoSavedSettings);

            ImVec2 lPos = ImGui::GetWindowPos();
            float  newX = lPos.x + lw;
            float  newY = lPos.y + lh * 0.5f;
            if (newX != SpeedoLabelX || newY != SpeedoLabelY)
            {
                SpeedoLabelX = newX;
                SpeedoLabelY = newY;
                SaveCurrentSettings();
            }
            ImGui::End();
        }
    }

    // Numeric mode
    if (!SpeedoTachometer)
    {
        return;
    }

    // Geometry
    bool  straightLine = SpeedoArcAngle < STRAIGHT_LINE_THRESHOLD;
    float axisAngleRad = SpeedoArcRotation * DEG_TO_RAD;
    float arcAngleRad  = 0.0f;
    float radius       = 0.0f;
    float pDist        = 0.0f;
    float arcMidAngle  = axisAngleRad;
    float arcStart     = 0.0f;
    float arcEnd       = 0.0f;
    float needleAngle  = 0.0f;

    if (!straightLine)
    {
        arcAngleRad    = std::fmin(SpeedoArcAngle * DEG_TO_RAD, TWO_PI * 0.999f);
        radius         = SpeedoArcLength / arcAngleRad;
        float maxPDist = std::fmin(500.0f, radius);
        pDist          = radius - std::fmin(SpeedoPDistance, maxPDist);
        arcStart       = axisAngleRad - arcAngleRad * 0.5f;
        arcEnd         = axisAngleRad + arcAngleRad * 0.5f;
        needleAngle    = arcStart + t * (arcEnd - arcStart);
    }

    ImVec2 P_local(
        std::cos(axisAngleRad) * pDist,
        std::sin(axisAngleRad) * pDist);

    // Compute a tight local-space bounding box around the arc/line plus the
    // needle origin P. This becomes the edit-mode ImGui window's size, so the
    // window hugs exactly what's drawn instead of using a fixed oversized
    // canvas. minX/minY/maxX/maxY are in the speedo's local (un-translated)
    // coordinate space, centered on C (0,0).
    const float padding = 8.0f;

    float minX =  1e9f, minY =  1e9f;
    float maxX = -1e9f, maxY = -1e9f;

    // The line is symmetric about the rotation axis, so its extent along each
    // world axis is always ±|projection| regardless of which way perpAngle
    // points — hence -abs(...) for the min side instead of the raw signed
    // value, which would be wrong half the time depending on rotation.
    if (straightLine)
    {
        float perpAngle = axisAngleRad + PI * 0.5f;
        float halfLen   = SpeedoArcLength * 0.5f;
        float cosPerp   = std::cos(perpAngle);
        float sinPerp   = std::sin(perpAngle);
        minX = std::min({ -std::abs(cosPerp * halfLen), P_local.x });
        minY = std::min({ -std::abs(sinPerp * halfLen), P_local.y });
        maxX = std::max({  std::abs(cosPerp * halfLen), P_local.x });
        maxY = std::max({  std::abs(sinPerp * halfLen), P_local.y });
    }
    else
    {
        float bboxRadius = radius;
        int   bboxSegs   = ArcSegments(bboxRadius, 1.0f);
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
    }

    float windowW = std::fmax((maxX - minX) + padding * 2.0f, 1.0f);
    float windowH = std::fmax((maxY - minY) + padding * 2.0f, 1.0f);
    float offsetX = -minX + padding;
    float offsetY = -minY + padding;

    ImGuiIO& io = ImGui::GetIO();
    float    wx = SpeedoWindowX < 0.0f ? io.DisplaySize.x * 0.5f - windowW * 0.5f : SpeedoWindowX;
    float    wy = SpeedoWindowY < 0.0f ? io.DisplaySize.y - windowH - 200.0f      : SpeedoWindowY;

    auto toScreen = [&](float lx, float ly) -> ImVec2 {
        return ImVec2(wx + lx + offsetX, wy + ly + offsetY);
    };

    // Face texture — always on background draw list so it's never clipped
    if (SpeedoFaceEnabled && s_faceTexture && s_faceTexture->Resource)
    {
        float w = s_faceTexture->Width  * SpeedoFaceScale;
        float h = s_faceTexture->Height * SpeedoFaceScale;
        ImGui::GetBackgroundDrawList()->AddImage(
            (ImTextureID)s_faceTexture->Resource,
            ImVec2(SpeedoFaceX, SpeedoFaceY),
            ImVec2(SpeedoFaceX + w, SpeedoFaceY + h));
    }

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

        // Face texture drag — handled outside the ImGui window so it can be
        // anywhere on screen. We use an invisible ImGui window sized to the
        // texture so the user can click and drag it independently.
        if (SpeedoFaceEnabled && s_faceTexture && s_faceTexture->Resource)
        {
            float fw = s_faceTexture->Width  * SpeedoFaceScale;
            float fh = s_faceTexture->Height * SpeedoFaceScale;

            ImGui::SetNextWindowPos(ImVec2(SpeedoFaceX, SpeedoFaceY), ImGuiCond_Once);
            ImGui::SetNextWindowSize(ImVec2(fw, fh), ImGuiCond_Always);
            ImGui::SetNextWindowBgAlpha(0.0f);
            ImGui::Begin("##speedo_face_drag", nullptr,
                ImGuiWindowFlags_NoDecoration       |
                ImGuiWindowFlags_NoFocusOnAppearing |
                ImGuiWindowFlags_NoNav              |
                ImGuiWindowFlags_NoScrollbar        |
                ImGuiWindowFlags_NoScrollWithMouse  |
                ImGuiWindowFlags_NoSavedSettings);

            ImVec2 facePos = ImGui::GetWindowPos();
            if (facePos.x != SpeedoFaceX || facePos.y != SpeedoFaceY)
            {
                SpeedoFaceX = facePos.x;
                SpeedoFaceY = facePos.y;
                SaveCurrentSettings();
            }
            ImGui::End();
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