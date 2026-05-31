// worldrender.cpp
// Draws checkpoint zone overlays directly onto the game world using ImGui's
// foreground draw list — the same technique used by most GW2 overlay addons.
//
// Both zone shapes are rendered as dot clouds:
//   Circle zone — a sphere of dots distributed via the golden-angle spiral,
//                 restricted to a configurable latitude band around the sphere.
//                 Dot alpha fades from full at the band centre to transparent
//                 at the upper and lower band edges.
//   Plane zone  — a rectangular grid of dots spanning the plane's width and a
//                 configurable vertical band.  The same centre/up/down band
//                 parameters control vertical extent and the alpha fade.
//
// Band parameters per RoutePoint:
//   bandCenterInpt — for Circle: centre latitude in degrees (-90 south … +90 north)
//                    for Plane:  vertical centre offset in metres above point.Y
//   bandUpInput    — extent upward   from centre (fade boundary; degrees / metres)
//   bandDownInput  — extent downward from centre (fade boundary; degrees / metres)
//
// Colour convention (set by RenderZones):
//   Green  — start checkpoint
//   Blue   — goal checkpoint
//   White  — intermediate checkpoints
//
// Zones are hidden automatically when the in-game map is open, and are
// only drawn for the current map (unless a checkpoint has MapID = 0).

#include "worldrender.h"
#include "shared.h"
#include "imgui.h"
#include "imgui_internal.h" 
#include <algorithm>
#include <cmath>
#include <deque>
#include <chrono>

// ---------------------------------------------------------------------------
// WorldToScreen
// ---------------------------------------------------------------------------
// Projects a world-space point (wx, wy, wz) onto screen space (sx, sy).
// Returns false when the point is behind the camera or far off-screen so
// callers can skip drawing that segment.
//
// The projection is a standard perspective transform built from scratch
// using MumbleLink's camera position and front vector, because GW2 doesn't
// expose a projection matrix directly.
//
// Steps:
//   1. Normalise the camera forward vector.
//   2. Derive the camera's right vector (rx, ry, rz) by crossing forward
//      with world-up (0,1,0).  Special-case: if the camera is nearly
//      straight up or down, use world-right (1,0,0) to avoid a zero cross.
//   3. Derive the camera's up vector (tx, ty, tz) as forward × right.
//   4. Transform the world-space offset into camera space by projecting
//      onto the three camera basis vectors.
//   5. Apply a perspective divide using the horizontal FOV, then remap
//      from [-1,1] NDC to pixel coordinates.
// ---------------------------------------------------------------------------
bool WorldToScreen(float wx, float wy, float wz, float& sx, float& sy)
{
    Vector3 camPos   = { GS.CameraX,      GS.CameraY,      GS.CameraZ      };
    Vector3 camFront = { GS.CameraFrontX, GS.CameraFrontY, GS.CameraFrontZ };

    // Step 1 — normalise the forward vector.
    float flen = std::sqrt(camFront.X*camFront.X + camFront.Y*camFront.Y + camFront.Z*camFront.Z);
    if (flen < 0.0001f) return false;
    float fx = camFront.X / flen;
    float fy = camFront.Y / flen;
    float fz = camFront.Z / flen;

    // Step 2 — derive the right vector (forward × world-up).
    float rx, ry, rz;
    if (std::abs(fy) > 0.999f)
    {
        rx = 1.0f; ry = 0.0f; rz = 0.0f;
    }
    else
    {
        rx = fz;
        ry = 0.0f;
        rz = -fx;
        float rlen = std::sqrt(rx*rx + rz*rz);
        rx /= rlen;
        rz /= rlen;
    }

    // Step 3 — derive the camera up vector as right × forward.
    float tx = fy*rz - fz*ry;
    float ty = fz*rx - fx*rz;
    float tz = fx*ry - fy*rx;

    // Step 4 — transform the world offset into camera space.
    float dx = wx - camPos.X;
    float dy = wy - camPos.Y;
    float dz = wz - camPos.Z;

    float vx =  rx*dx + ry*dy + rz*dz;
    float vy =  tx*dx + ty*dy + tz*dz;
    float vz =  fx*dx + fy*dy + fz*dz;

    if (vz <= 0.01f) return false;

    // Step 5 — perspective projection.
    ImGuiIO& io  = ImGui::GetIO();
    float aspect = io.DisplaySize.x / io.DisplaySize.y;
    float fov = (GS.FOV > 0.01f) ? GS.FOV : 0.873f;
    float f   = 1.0f / std::tan(fov * 0.5f);

    float px =  vx * (f / aspect) / vz;
    float py = -vy * f / vz;

    sx = (px * 0.5f + 0.5f) * io.DisplaySize.x;
    sy = (py * 0.5f + 0.5f) * io.DisplaySize.y;

    if (sx < -10000 || sx > 10000 || sy < -10000 || sy > 10000) return false;

    return true;
}

// ---------------------------------------------------------------------------
// RoutePointIsSet  (file-private helper)
// ---------------------------------------------------------------------------
static bool RoutePointIsSet(const RoutePoint& point)
{
    return point.X != 0.0f || point.Y != 0.0f || point.Z != 0.0f;
}


// ---------------------------------------------------------------------------
// OcclusionState  (file-private helper)
// ---------------------------------------------------------------------------
// Computed once per frame (via CalcOcclusionState) and passed into both zone
// renderers so player-occlusion logic doesn't have to be duplicated.
// ---------------------------------------------------------------------------
struct OcclusionState
{
    bool  playerOnScreen;
    float playerSx, playerSy;
    float occludeRadius;
};

static OcclusionState CalcOcclusionState()
{
    OcclusionState os;

    os.playerOnScreen = WorldToScreen(
        GS.PlayerX, GS.PlayerY + 1.0f, GS.PlayerZ,
        os.playerSx, os.playerSy);

    float camToPlayerX = GS.CameraX - GS.PlayerX;
    float camToPlayerY = GS.CameraY - GS.PlayerY;
    float camToPlayerZ = GS.CameraZ - GS.PlayerZ;
    float camToPlayer  = std::sqrt(camToPlayerX*camToPlayerX + camToPlayerY*camToPlayerY + camToPlayerZ*camToPlayerZ);

    // Pixel radius of the occlusion circle — larger when camera is close, smaller when far
    os.occludeRadius = std::clamp(occludePixelRadius / (camToPlayer * 0.5f), 30.0f, occludePixelClamp);

    return os;
}

// Cached UI window rects from the previous frame.
// Updated at the start of RenderZones() so we don't check mid-submission state.
static std::vector<ImRect> s_uiRects;

static void UpdateUIRects()
{
    s_uiRects.clear();
    ImGuiContext& g = *ImGui::GetCurrentContext();
    for (ImGuiWindow* window : g.Windows)
    {
        if (!window->WasActive)  continue;  // WasActive = confirmed visible last frame
        if (window->Hidden)      continue;
        if (window->Flags & ImGuiWindowFlags_NoBackground) continue;
        s_uiRects.push_back(window->Rect());
    }
}

static bool IsOccludedByUI(float sx, float sy)
{
    ImVec2 p(sx, sy);
    for (const ImRect& r : s_uiRects)
        if (r.Contains(p)) return true;
    return false;
}

// Applies occlusion fade to dotAlpha based on the dot's screen position.
static int ApplyOcclusion(int dotAlpha, float sx, float sy, const OcclusionState& os)
{
    if (IsOccludedByUI(sx, sy)) return 0;

    if (!os.playerOnScreen) return dotAlpha;
    float ddx  = sx - os.playerSx;
    float ddy  = sy - os.playerSy;
    float dist = std::sqrt(ddx*ddx + ddy*ddy);
    float occludeFade = std::clamp((dist - os.occludeRadius * 0.6f) / (os.occludeRadius * 1.0f), 0.0f, 1.0f);
    return (int)(dotAlpha * occludeFade);
}

// PI
constexpr float PIf = 3.14159265358979323846f;

// ---------------------------------------------------------------------------
// RenderZoneCircle
// ---------------------------------------------------------------------------
// Draws a world-space zone indicator for sphere-type triggers.
//
// Dots are distributed evenly across the sphere surface using the
// golden-angle spiral, but only within the latitude band defined by the
// point's band parameters:
//
//   bandCenterInput — centre latitude of the rendered band (-90 south … +90 north)
//   bandUpInput     — how far above the centre the band extends (fade to 0 at edge)
//   bandDownInput   — how far below the centre the band extends (fade to 0 at edge)
//
// All NUM_DOTS dots are placed within this band, so none are wasted outside
// the visible range.  Each dot's alpha is the product of:
//   • Band falloff  — 1.0 at band centre, 0.0 at either edge.
//   • Distance fade — per-dot world-space distance to the player mapped
//                     through ZoneFadeStart/ZoneFadeEnd.
//   • Occlusion     — dots behind the player model are faded via ApplyOcclusion.
// ---------------------------------------------------------------------------
void RenderZoneCircle(const RoutePoint& point, float r, float g, float b)
{
    if (!MumbleLink && !GS.RTAPIAvailable) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    const float DOT_RADIUS = 3.0f;
    const int NUM_DOTS = point.DotDensity > 0 ? point.DotDensity : 300;

    const float golden = PIf * (3.0f - std::sqrt(5.0f)); // golden angle ~2.399 radians

    OcclusionState os = CalcOcclusionState();

    // --- Combat pulse (out-of-combat only) ---
    // Mimics a heartbeat: two sharp gaussian bumps (lub-dub) followed by a
    // long flat rest, repeating at ~60 BPM (1-second cycle).
    // Each bump is a gaussian centred at a fixed phase offset within the cycle.
    // The radius offset is the sum of the two bumps, giving a ±0.5 m swing.
    float effectiveRadius = point.RadiusWidth;
    if (point.TriggerType == ETriggerType::CombatArena && !GS.IsInCombat)
    {
        const float BPM         = 20.0f;
        const float cycleSecs   = 60.0f / BPM;                              // 1.0 s per beat
        const float phase       = std::fmod((float)ImGui::GetTime(), cycleSecs) / cycleSecs; // [0,1)

        // Two gaussian peaks within the cycle: "lub" at 8%, "dub" at 22%
        // Width (sigma) kept tight so they decay quickly into the flat rest.
        auto gauss = [](float x, float mu, float sigma) -> float {
            float d = (x - mu) / sigma;
            return std::exp(-0.5f * d * d);
        };

        float lub = gauss(phase, 0.08f, 0.035f);
        float dub = gauss(phase, 0.22f, 0.035f) * 0.65f; // dub is a bit softer

        // Combine and remap so the resting baseline sits at -0.5 m and the
        // peak of "lub" reaches +0.5 m — a total swing of 1 m.
        float pulse = (lub + dub);                   // [0, ~1.65] at peaks, ~0 at rest
        effectiveRadius += pulse * 0.5f - 0.05f;     // offset so rest ≈ nominal radius
    }

    // Convert band parameters to radians
    const float bandCenter = point.bandCenterInput * (PIf / 180.0f);
    const float bandUp     = point.bandUpInput     * (PIf / 180.0f);
    const float bandDown   = point.bandDownInput   * (PIf / 180.0f);

    // Derived latitude limits, clamped to the valid sphere range
    const float phiMinClamped = std::max(bandCenter - bandDown, -PIf * 0.5f);
    const float phiMaxClamped = std::min(bandCenter + bandUp,    PIf * 0.5f);

    // t values corresponding to phiMin/phiMax in the asin distribution
    const float tMin = (std::sin(phiMinClamped) + 1.0f) * 0.5f;
    const float tMax = (std::sin(phiMaxClamped) + 1.0f) * 0.5f;

    for (int i = 0; i < NUM_DOTS; i++)
    {
        // Remap i into [tMin, tMax] so all dots land within the visible band
        const float t   = tMin + (tMax - tMin) * (float)i / (NUM_DOTS - 1);
        float phi   = std::asin(-1.0f + 2.0f * t); // latitude within the band
        float theta = golden * i;                    // longitude

        // Falloff: 1.0 at band centre, 0.0 at either edge
        float distFromCenter = phi - bandCenter;
        float normalizedDist = (distFromCenter >= 0.0f)
            ? ((bandUp   > 0.0f) ? distFromCenter /  bandUp   : 0.0f)
            : ((bandDown > 0.0f) ? distFromCenter / -bandDown : 0.0f);

        float falloff  = 1.0f - std::abs(normalizedDist);

        // --- Rotating gap (Interact trigger only) ---
        // A fixed arc of longitude is hidden and the whole pattern rotates,
        // giving a "beckoning" sweep effect.
        // The gap edges are softened over a feather band so the cut fades
        // in/out rather than clipping hard.
        float interactAlpha = 1.0f;
        if (point.TriggerType == ETriggerType::CircleInteract)
        {
            const float gapDeg     = 90.0f;                              // degrees of arc to hide
            const float featherDeg = 15.0f;                              // soft fade either side
            const float rpm        = 20.0f;                              // rotation speed
            const float gapRad     = gapDeg     * (PIf / 180.0f);
            const float featherRad = featherDeg * (PIf / 180.0f);

            // Advance theta by time, normalise into [0, 2π)
            float rotOffset = std::fmod((float)ImGui::GetTime() * (rpm / 60.0f) * 2.0f * PIf, 2.0f * PIf);
            float thetaNorm = std::fmod(theta - rotOffset + 4.0f * PIf, 2.0f * PIf);

            // [0, gapRad]            → fully hidden
            // [gapRad, gapRad+feath] → leading feather (fade back in)
            // [2π-feath, 2π]         → trailing feather (fade out into gap)
            if (thetaNorm < gapRad + featherRad)
            {
                if (thetaNorm < gapRad)
                    interactAlpha = 0.0f;
                else
                    interactAlpha = (thetaNorm - gapRad) / featherRad; // 0→1
            }
            float trailingDist = 2.0f * PIf - thetaNorm;
            if (trailingDist < featherRad)
                interactAlpha = std::min(interactAlpha, trailingDist / featherRad); // 1→0
        }

        float wx = point.X + std::cos(phi) * std::cos(theta) * effectiveRadius;
        float wy = point.Y + std::sin(phi)                   * effectiveRadius;
        float wz = point.Z + std::cos(phi) * std::sin(theta) * effectiveRadius;

        // Per-dot distance fade from player position using the global fade range
        float pdx = wx - GS.PlayerX;
        float pdy = wy - GS.PlayerY;
        float pdz = wz - GS.PlayerZ;
        float playerDist = std::sqrt(pdx*pdx + pdy*pdy + pdz*pdz);
        float dotDistAlpha = std::clamp(1.0f - (playerDist - ZoneFadeStart) / (ZoneFadeEnd - ZoneFadeStart), 0.0f, 1.0f);

        int   dotAlpha = (int)(220 * 0.8f * falloff * dotDistAlpha * interactAlpha);

        float sx, sy;
        if (WorldToScreen(wx, wy, wz, sx, sy))
        {
            dl->AddCircleFilled(ImVec2(sx, sy), DOT_RADIUS,
                IM_COL32((int)(r*255), (int)(g*255), (int)(b*255),
                    ApplyOcclusion(dotAlpha, sx, sy, os)));
        }
    }
}

// ---------------------------------------------------------------------------
// RenderZonePlane
// ---------------------------------------------------------------------------
// Draws a world-space zone indicator for Plane triggers as a grid of dots.
//
// Dots are laid out on the plane surface in a regular grid:
//   • Horizontally: evenly spaced across the full RadiusWidth.
//   • Vertically:   evenly spaced within the height band defined by the
//                   point's band parameters (in metres, not degrees):
//
//       bandCenterInput — vertical centre offset in metres above point.Y
//       bandUpInput     — metres above centre where alpha reaches 0
//       bandDownInput   — metres below centre where alpha reaches 0
//
// Dot density is derived automatically from DOT_SPACING so wider or taller
// planes fill in without needing a manual dot count.
//
// Each dot's alpha is the product of band falloff (1.0 at centre, 0.0 at
// edges), a per-dot distance fade from the player through ZoneFadeStart/
// ZoneFadeEnd, and occlusion fade via ApplyOcclusion — matching the sphere
// zone's visual language exactly.
// ---------------------------------------------------------------------------
void RenderZonePlane(const RoutePoint& point, float r, float g, float b)
{
    if (!MumbleLink && !GS.RTAPIAvailable) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    OcclusionState os = CalcOcclusionState();

    // Along-plane direction vector from PlaneAngle
    float angleRad = point.PlaneAngle * PIf / 180.0f;
    float px = -std::sin(angleRad);
    float pz =  std::cos(angleRad);

    float halfWidth = point.RadiusWidth * 0.5f;

    // Reuse band fields as world-space height values (metres, not degrees)
    float bandCenter = point.bandCenterInput; // vertical centre above point.Y
    float bandUp     = point.bandUpInput;     // height above centre → fade to 0
    float bandDown   = point.bandDownInput;   // depth below centre → fade to 0

    float yMin = point.Y + bandCenter - bandDown;
    float yMax = point.Y + bandCenter + bandUp;
    float yCtr = point.Y + bandCenter;

    const float e30   = std::log(2.0f)  / std::log(200.0f / 30.0f);
    const float e1000 = std::log(0.2f)  / std::log(200.0f / 1000.0f);

    float t = std::clamp((point.DotDensity - 30.0f) / 970.0f, 0.0f, 1.0f);
    float e = e30 + (e1000 - e30) * t;

    float DOT_SPACING = std::pow(200.0f / point.DotDensity, e);
    const float DOT_RADIUS  = 3.0f;
    int   cols = std::max(2, (int)(point.RadiusWidth / DOT_SPACING) + 1);
    int   rows = std::max(2, (int)((bandUp + bandDown) / DOT_SPACING) + 1);

    for (int ci = 0; ci < cols; ci++)
    {
        // t in [0,1] along the width axis
        float t  = (cols > 1) ? (float)ci / (cols - 1) : 0.5f;
        float wx = point.X + px * (-halfWidth + t * point.RadiusWidth);
        float wz = point.Z + pz * (-halfWidth + t * point.RadiusWidth);

        for (int ri = 0; ri < rows; ri++)
        {
            float s  = (rows > 1) ? (float)ri / (rows - 1) : 0.5f;
            float wy = yMin + s * (yMax - yMin);

            // Falloff: 1.0 at band centre, 0.0 at top and bottom edges
            float distFromCenter = wy - yCtr;
            float normalizedDist = (distFromCenter >= 0.0f)
                ? ((bandUp   > 0.0f) ? distFromCenter /  bandUp   : 0.0f)
                : ((bandDown > 0.0f) ? distFromCenter / -bandDown : 0.0f);

            float falloff  = 1.0f - std::abs(normalizedDist);

            // Per-dot distance fade from player position using the global fade range
            float pdx = wx - GS.PlayerX;
            float pdy = wy - GS.PlayerY;
            float pdz = wz - GS.PlayerZ;
            float playerDist = std::sqrt(pdx*pdx + pdy*pdy + pdz*pdz);
            float dotDistAlpha = std::clamp(1.0f - (playerDist - ZoneFadeStart) / (ZoneFadeEnd - ZoneFadeStart), 0.0f, 1.0f);

            int   dotAlpha = (int)(220 * falloff * dotDistAlpha);

            float sx, sy;
            if (WorldToScreen(wx, wy, wz, sx, sy))
            {
                dl->AddCircleFilled(ImVec2(sx, sy), DOT_RADIUS,
                    IM_COL32((int)(r*255), (int)(g*255), (int)(b*255),
                        ApplyOcclusion(dotAlpha, sx, sy, os)));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// RenderZoneMap
// ---------------------------------------------------------------------------
// Draws a screen-space indicator for MapChange trigger zones as a dot field
// in the upper-left corner of the screen.
//
// Layout:
//   • Dots are arranged on a regular grid with SPACING pixel pitch, starting
//     at (DOT_RADIUS, DOT_RADIUS) so edge dots are fully visible.
//   • Only dots where x * y < C are drawn, producing a hyperbolic cutout
//     with C = 1200 controlling the curve's openness.
//
// Alpha modulation (multiplicative):
//   • Hyperbola fade — 1.0 near the corner, falling to 0 at the curve edge,
//                      driven by (x * y) / C.
//   • Arm fade       — dots along the axis arms fade toward the arm tips,
//                      driven by max(x, y) / ARM_LEN.  This ensures both
//                      the hyperbola edge and the arm ends dissolve smoothly
//                      rather than clipping hard.
//   • Mouse repel    — dots within MouseRepelRadius px of the cursor fade to 0,
//                      with a soft falloff over the inner half of that radius.
//   • UI occlusion   — dots covered by any active ImGui window are hidden
//                      entirely via IsOccludedByUI, using the rect snapshot
//                      taken by UpdateUIRects() at the start of RenderZones().
//
// The colour follows the same green/blue/white convention as the other zone
// renderers and is passed in by RenderZones.
// ---------------------------------------------------------------------------
void RenderZoneMap(const RoutePoint& point, float r, float g, float b)
{
    ImDrawList* dl  = ImGui::GetForegroundDrawList();
    ImVec2 mousePos = ImGui::GetMousePos();

    const float C            = point.HyperbolaC * 100;
    const float SPACING      = 6.0f;
    const float DOT_RADIUS   = 3.0f;
    const float ARM_LEN      = 300.0f;
    const float POWER        = 1.5f;
    const float MouseRepelRadius = 50.0f;

    float x = DOT_RADIUS;
    while (x < ARM_LEN)
    {
        float y = DOT_RADIUS;
        while (y < ARM_LEN)
        {
            if (x * y < C)
            {
                float t_hyp    = (x * y) / C;
                float hyp_fade = std::pow(1.0f - t_hyp, POWER);

                float t_arm    = std::max(x, y) / ARM_LEN;
                float arm_fade = std::pow(std::max(0.0f, 1.0f - t_arm), POWER);

                float alpha = hyp_fade * arm_fade;
                if (alpha > 0.02f)
                {
                    // UI occlusion
                    if (IsOccludedByUI(x, y)) { y += SPACING; continue; }

                    // Mouse repel
                    float mdx        = x - mousePos.x;
                    float mdy        = y - mousePos.y;
                    float mouseDist  = std::sqrt(mdx * mdx + mdy * mdy);
                    float mouseAlpha = std::clamp(
                        (mouseDist - MouseRepelRadius * 0.5f) / (MouseRepelRadius * 0.5f),
                        0.0f, 1.0f);

                    int a = (int)(alpha * 255.0f * mouseAlpha);
                    if (a > 0)
                    {
                        dl->AddCircleFilled(ImVec2(x, y), DOT_RADIUS,
                            IM_COL32((int)(r * 255), (int)(g * 255), (int)(b * 255), a));
                    }
                }
            }
            y += SPACING;
        }
        x += SPACING;
    }
}

// ---------------------------------------------------------------------------
// RenderZones
// ---------------------------------------------------------------------------
// Entry point called every frame from AddonRender(). Iterates the active
// route and dispatches each checkpoint to the right shape renderer.
//
// Filtering rules:
//   • Hidden when the in-game world map is open (zones would clutter the map UI).
//   • MapChange checkpoints are drawn via RenderZoneMap (screen-space corner).
//   • Checkpoints with MapID != 0 are only drawn on their configured map.
//   • Unplaced checkpoints (all-zero position) are skipped.
//
// Draw order: start (green) → goal (blue) → all intermediate (white).
// The start and goal are handled separately so they are always drawn first
// and can't be skipped by the intermediate checkpoint loop.
// ---------------------------------------------------------------------------
void RenderZones()
{
    if (!ShowZones) return;
    // GS.IsMapOpen is always sourced from Mumble (RTAPI does not expose this
    // flag); skip rendering while the in-game map is fullscreen.
    if (GS.IsMapOpen) return;

    UpdateUIRects(); // snapshot UI window rects before drawing any dots
    
    // Map ID and camera position come from GS, populated each frame from
    // whichever source is active (RTAPI or Mumble).
    unsigned int currMapID = GS.MapID;

    auto shouldRender = [&](const RoutePoint& p) -> bool
    {
        if (!RoutePointIsSet(p))                          return false;
        if (p.MapID == 0)                                 return true;
        // MapChange zones render on any map when MapID == 0, or on their configured map only.
        if (p.TriggerType == ETriggerType::MapChange)     return p.MapID == 0 || currMapID == p.MapID;
        return currMapID == p.MapID;
    };

    // Rolling 1-second average render time for the selected debug checkpoint.
    // The deque and last-known index are kept as statics so they survive frames.
    static std::deque<std::pair<float,float>> s_timingSamples; // {ms, timestamp}
    static int s_lastTimedIndex = -1;
    
    auto renderPoint = [&](const RoutePoint& p, float r, float g, float b, float dbgOffset, int idx)
    {
        if (!shouldRender(p)) return;

        // Hide triggered checkpoints
        if (idx >= 0 && idx < (int)checkpointTriggered.size() && checkpointTriggered[idx])
            return;
    
        // MapChange zones are screen-space — skip world-space distance culling.
        if (p.TriggerType != ETriggerType::MapChange)
        {
        // Broad-phase cull: skip the zone only when the player is so far from
        // the zone centre that even the nearest dot on the surface is beyond
        // ZoneFadeEnd.  The extent used depends on trigger type:
        //   Circle — sphere radius
        //   Plane  — half-diagonal of width × height band, so corner dots are covered
        float extent;
        if (p.TriggerType == ETriggerType::Plane)
        {
            float halfW = p.RadiusWidth * 0.5f;
            float halfH = (p.bandUpInput + p.bandDownInput) * 0.5f; // metres
            extent = std::sqrt(halfW*halfW + halfH*halfH);
        }
        else
        {
            extent = p.RadiusWidth;
        }
        float fdx = GS.PlayerX - p.X;
        float fdy = GS.PlayerY - p.Y;
        float fdz = GS.PlayerZ - p.Z;
        float fdist = std::sqrt(fdx*fdx + fdy*fdy + fdz*fdz);
        if (fdist > ZoneFadeEnd + extent) return;
        } // end broad-phase cull
    
        bool isTimed = ShowDebug && (idx == ZoneRenderSelectedIndex);
    
        if (isTimed)
        {
            // Clear samples if selection changed.
            if (s_lastTimedIndex != idx)
            {
                s_timingSamples.clear();
                s_lastTimedIndex = idx;
            }
    
            auto t0 = std::chrono::high_resolution_clock::now();
    
            if (p.TriggerType == ETriggerType::Plane)
                RenderZonePlane(p, r, g, b);
            else if (p.TriggerType == ETriggerType::MapChange)
                RenderZoneMap(p, r, g, b);
            else
                RenderZoneCircle(p, r, g, b);
    
            float ms = std::chrono::duration<float, std::milli>(
                std::chrono::high_resolution_clock::now() - t0).count();
    
            float now = (float)ImGui::GetTime();
            s_timingSamples.push_back({ ms, now });
    
            // Drop samples older than 1 second.
            while (!s_timingSamples.empty() && (now - s_timingSamples.front().second) > 1.0f)
                s_timingSamples.pop_front();
    
            // Update the shared average.
            float sum = 0.0f;
            for (auto& s : s_timingSamples) sum += s.first;
            ZoneRenderAvgMs = s_timingSamples.empty() ? 0.0f : sum / (float)s_timingSamples.size();
        }
        else
        {
            if (p.TriggerType == ETriggerType::Plane)
                RenderZonePlane(p, r, g, b);
            else if (p.TriggerType == ETriggerType::MapChange)
                RenderZoneMap(p, r, g, b);
            else
                RenderZoneCircle(p, r, g, b);
        }
    };

    int startIdx = -1, goalIdx = -1;
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        if (CurrentRoute.Checkpoints[i].IsStart) startIdx = i;
        if (CurrentRoute.Checkpoints[i].IsGoal)  goalIdx  = i;
    }

    Checkpoint* start = GetStart(CurrentRoute);
    Checkpoint* goal  = GetGoal(CurrentRoute);

    if (start) renderPoint(start->Point, 0.2f, 1.0f, 0.2f, 0.0f,  startIdx);
    if (goal)  renderPoint(goal->Point,  0.2f, 0.5f, 1.0f, 80.0f, goalIdx);

    int dbgIdx = 0;
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const Checkpoint& cp = CurrentRoute.Checkpoints[i];
        if (cp.IsStart || cp.IsGoal) continue;
        renderPoint(cp.Point, 1.0f, 1.0f, 1.0f, 160.0f + dbgIdx * 80.0f, i);
        dbgIdx++;
    }
}