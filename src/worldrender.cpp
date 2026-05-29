// worldrender.cpp
// Draws checkpoint zone overlays directly onto the game world using ImGui's
// foreground draw list — the same technique used by most GW2 overlay addons.
//
// Two visual shapes are supported:
//   Circle zone — two wireframe rings outlining the sphere boundary, plus a
//                 translucent filled dome fading from 40% alpha at the base
//                 to transparent at the top (Y + Radius).
//   Plane zone  — a filled translucent wall starting at Y, fading to
//                 transparent at the top, with a solid outline on the edges.
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
#include <algorithm>
#include <cmath>

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
    float f      = 1.0f / std::tan(CameraFOV * 0.5f);

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
// DrawGradientTriangle  (file-private helper)
// ---------------------------------------------------------------------------
// Draws a single triangle with per-vertex colours using low-level PrimVtx
// calls, since AddTriangleFilledMultiColor is not available in this ImGui version.
// ---------------------------------------------------------------------------
static void DrawGradientTriangle(ImDrawList* dl,
    ImVec2 p0, ImU32 c0,
    ImVec2 p1, ImU32 c1,
    ImVec2 p2, ImU32 c2)
{
    ImVec2 uv = ImGui::GetIO().Fonts->TexUvWhitePixel;
    dl->PrimReserve(3, 3);
    dl->PrimVtx(p0, uv, c0);
    dl->PrimVtx(p1, uv, c1);
    dl->PrimVtx(p2, uv, c2);
}

// ---------------------------------------------------------------------------
// RenderZoneCircle
// ---------------------------------------------------------------------------
// Draws a world-space zone indicator for sphere-type triggers.
//
// Two modes controlled by point.DotSphereCount:
//
//   DotSphereCount == 0 (default) — horizontal wireframe ring on the XZ plane
//                     plus a filled base band fading from BASE_ALPHA at Y to
//                     transparent at Y+1.  Best for ground-based triggers
//                     where the flat ring communicates the trigger boundary
//                     clearly.
//
//   DotSphereCount  > 0           — a sphere made of projected dots,
//                     distributed evenly using the golden-angle spiral.
//                     Dots outside ±60° latitude are skipped (avoiding polar
//                     crowding) and dots occluded by the player model are
//                     faded out.  Best for triggers that extend significantly
//                     above ground, or wherever showing the full 3D volume
//                     is more readable than a flat ring.
//
// Both modes fade out as the camera moves inside the trigger radius (to avoid
// a distracting full-screen fill when the player is in the zone) and as the
// zone moves outside the configured ZoneFadeStart/ZoneFadeEnd range.
// ---------------------------------------------------------------------------
void RenderZoneCircle(const RoutePoint& point, float r, float g, float b, float debugOffsetY, float distAlpha)
{
    // Require at least one live data source — camera data comes from GS which
    // is only meaningful when Mumble or RTAPI has been initialised.
    if (!MumbleLink && !GS.RTAPIAvailable) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // --- Fade when camera is close to the zone centre ---
    float camDx = GS.CameraX - point.X;
    float camDy = GS.CameraY - point.Y;
    float camDz = GS.CameraZ - point.Z;
    float camDist = std::sqrt(camDx*camDx + camDy*camDy + camDz*camDz);
    float fadeStart = point.Radius * 1.5f;
    float fadeEnd   = point.Radius * 0.8f;
    float alpha = std::clamp((camDist - fadeEnd) / (fadeStart - fadeEnd), 0.0f, 1.0f);

    const int steps = 64;

    // --- Wireframe rings ---
    auto DrawRing = [&](auto getPoint, int ringAlpha)
    {
        float prevSx = 0, prevSy = 0;
        bool  prevValid = false;
        ImU32 color = IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), ringAlpha);

        for (int i = 0; i <= steps; i++)
        {
            float angle = (float)i / steps * 2.0f * 3.14159265f;
            auto [wx, wy, wz] = getPoint(angle);

            float sx, sy;
            bool valid = WorldToScreen(wx, wy, wz, sx, sy);

            if (valid && prevValid)
                dl->AddLine(ImVec2(prevSx, prevSy), ImVec2(sx, sy), color, 2.0f);

            prevSx    = sx;
            prevSy    = sy;
            prevValid = valid;
        }
    };

    if (point.DotSphereCount > 0)
    {
        const float PI = 3.14159265f;
        const float DOT_RADIUS = 3.0f;
        const int NUM_DOTS = point.DotSphereCount;
    
        if (distAlpha <= 0.0f) return; // early out, nothing to draw
    
        const float golden = PI * (3.0f - std::sqrt(5.0f)); // golden angle ~2.399 radians
    
        // Project the player to screen once before the dot loop
        float playerSx, playerSy;
        bool playerOnScreen = WorldToScreen(
            GS.PlayerX,
            GS.PlayerY + 1.0f,
            GS.PlayerZ,
            playerSx, playerSy);

        // Camera distance to player for occlusion radius scaling
        float camToPlayerX = GS.CameraX - GS.PlayerX;
        float camToPlayerY = GS.CameraY - GS.PlayerY;
        float camToPlayerZ = GS.CameraZ - GS.PlayerZ;
        float camToPlayer  = std::sqrt(camToPlayerX*camToPlayerX + camToPlayerY*camToPlayerY + camToPlayerZ*camToPlayerZ);

        // Radius in pixels — larger when camera is close, smaller when far
        float occludeRadius = std::clamp(occludePixelRadius / (camToPlayer * 0.5f), 30.0f, occludePixelClamp);

        for (int i = 0; i < NUM_DOTS; i++)
        {
            float phi   = std::asin(-1.0f + 2.0f * (float)i / (NUM_DOTS - 1)); // latitude -90 to +90
            float theta = golden * i;                                            // longitude
    
            // Skip dots outside ±60 degrees latitude
            if (std::abs(phi) > PI / 3.0f) continue;
    
            float falloff  = 1.0f - std::abs(phi) / (PI / 3.0f);
            int   dotAlpha = (int)(220 * alpha * 0.8f * falloff * distAlpha);
    
            float wx = point.X + std::cos(phi) * std::cos(theta) * point.Radius;
            float wy = point.Y + std::sin(phi) * point.Radius;
            float wz = point.Z + std::cos(phi) * std::sin(theta) * point.Radius;
    
            float sx, sy;
            if (WorldToScreen(wx, wy, wz, sx, sy))
            {
                int fadedAlpha = dotAlpha;
            
                if (playerOnScreen)
                {
                    float ddx  = sx - playerSx;
                    float ddy  = sy - playerSy;
                    float dist = std::sqrt(ddx*ddx + ddy*ddy);
                    float occludeFade = std::clamp((dist - occludeRadius * 0.6f) / (occludeRadius * 1.0f), 0.0f, 1.0f);
                    fadedAlpha = (int)(dotAlpha * occludeFade);
                }
            
                dl->AddCircleFilled(ImVec2(sx, sy), DOT_RADIUS,
                    IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), fadedAlpha));
            }
        }
    }
    else
    {
        // --- Default mode ---
        // Horizontal ring + filled base band.

        // Horizontal ring — flat on the XZ plane at point.Y
        DrawRing([&](float a) {
            return std::make_tuple(
                point.X + std::cos(a) * point.Radius,
                point.Y,
                point.Z + std::sin(a) * point.Radius);
        }, (int)(200 * alpha * distAlpha));

        // --- Filled base band ---
        // Two rings projected at Y (full alpha) and Y+1 (transparent),
        // connected with gradient quads. Reads as a soft glowing ground ring
        // rather than a cone or dome.
        const float BASE_ALPHA = 100.0f * alpha * distAlpha;
        const int   bandSteps  = 32;

        ImU32 baseColor = IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), (int)BASE_ALPHA);
        ImU32 topColor  = IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 0);

        float prevBotSx = 0, prevBotSy = 0;
        float prevTopSx = 0, prevTopSy = 0;
        bool  prevBotValid = false, prevTopValid = false;

        for (int i = 0; i <= bandSteps; i++)
        {
            float angle = (float)i / bandSteps * 2.0f * 3.14159265f;
            float wx = point.X + std::cos(angle) * point.Radius;
            float wz = point.Z + std::sin(angle) * point.Radius;

            float botSx, botSy, topSx, topSy;
            bool botValid = WorldToScreen(wx, point.Y,        wz, botSx, botSy);
            bool topValid = WorldToScreen(wx, point.Y + 1.0f, wz, topSx, topSy);

            if (botValid && topValid && prevBotValid && prevTopValid)
            {
                // Quad as two gradient triangles.
                DrawGradientTriangle(dl,
                    ImVec2(prevBotSx, prevBotSy), baseColor,
                    ImVec2(botSx, botSy),         baseColor,
                    ImVec2(prevTopSx, prevTopSy), topColor);
                DrawGradientTriangle(dl,
                    ImVec2(botSx, botSy),         baseColor,
                    ImVec2(topSx, topSy),         topColor,
                    ImVec2(prevTopSx, prevTopSy), topColor);
            }

            prevBotSx    = botSx;    prevBotSy    = botSy;
            prevTopSx    = topSx;    prevTopSy    = topSy;
            prevBotValid = botValid; prevTopValid = topValid;
        }
    }
}

// ---------------------------------------------------------------------------
// RenderZonePlane
// ---------------------------------------------------------------------------
// Draws a filled translucent wall for a Plane trigger.
//
// The wall starts at Y (ground level, assuming the author placed it there)
// and extends upward. It is split into two vertical bands:
//
//   Lower band (Y to Y + SOLID_HEIGHT) — full BASE_ALPHA, communicates
//              where the player actually crosses the trigger.
//   Upper band (Y + SOLID_HEIGHT to Y + FADE_HEIGHT) — alpha fades to 0,
//              communicating that the plane extends infinitely upward.
//
// The left and right outline edges are kept as solid lines so the width and
// angle of the plane are immediately readable. The top and bottom edges are
// omitted — the bottom is at ground level (implicit) and the top fades out.
// The diagonal cross is removed since the fill makes orientation obvious.
// ---------------------------------------------------------------------------
void RenderZonePlane(const RoutePoint& point, float r, float g, float b, float distAlpha)
{
    // Require at least one live data source (same rationale as RenderZoneCircle).
    if (!MumbleLink && !GS.RTAPIAvailable) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // Along-plane direction vector from PlaneAngle.
    float angleRad = point.PlaneAngle * 3.14159265f / 180.0f;
    float px = -std::sin(angleRad);
    float pz =  std::cos(angleRad);

    float halfWidth  = point.PlaneWidth * 0.5f;
    float solidHeight = 3.0f;  // Height of the fully opaque lower band
    float fadeHeight  = 10.0f; // Height at which the wall is fully transparent

    float yBottom = point.Y;
    float yMid    = point.Y + solidHeight;
    float yTop    = point.Y + fadeHeight;

    // Left and right X/Z endpoints
    float wx0 = point.X - px * halfWidth;
    float wz0 = point.Z - pz * halfWidth;
    float wx1 = point.X + px * halfWidth;
    float wz1 = point.Z + pz * halfWidth;

    // Project all six points (bottom, mid, top for each side)
    float s0x, s0y, s1x, s1y; // Bottom-left, Bottom-right
    float s2x, s2y, s3x, s3y; // Mid-left,    Mid-right
    float s4x, s4y, s5x, s5y; // Top-left,    Top-right

    bool v0 = WorldToScreen(wx0, yBottom, wz0, s0x, s0y);
    bool v1 = WorldToScreen(wx1, yBottom, wz1, s1x, s1y);
    bool v2 = WorldToScreen(wx0, yMid,    wz0, s2x, s2y);
    bool v3 = WorldToScreen(wx1, yMid,    wz1, s3x, s3y);
    bool v4 = WorldToScreen(wx0, yTop,    wz0, s4x, s4y);
    bool v5 = WorldToScreen(wx1, yTop,    wz1, s5x, s5y);

    const int   ri         = (int)(r * 255);
    const int   gi         = (int)(g * 255);
    const int   bi         = (int)(b * 255);
    const float BASE_ALPHA = 100.0f * distAlpha;

    ImU32 solidColor = IM_COL32(ri, gi, bi, (int)BASE_ALPHA);
    ImU32 fadeColor  = IM_COL32(ri, gi, bi, 0);
    ImU32 lineColor = IM_COL32(ri, gi, bi, (int)(200 * distAlpha));

    // --- Lower band: bottom → mid, fully opaque ---
    // Two triangles forming the solid quad.
    if (v0 && v1 && v2 && v3)
    {
        DrawGradientTriangle(dl,
            ImVec2(s0x, s0y), solidColor,
            ImVec2(s1x, s1y), solidColor,
            ImVec2(s2x, s2y), solidColor);
        DrawGradientTriangle(dl,
            ImVec2(s1x, s1y), solidColor,
            ImVec2(s3x, s3y), solidColor,
            ImVec2(s2x, s2y), solidColor);
    }

    // --- Upper band: mid → top, fading to transparent ---
    if (v2 && v3 && v4 && v5)
    {
        DrawGradientTriangle(dl,
            ImVec2(s2x, s2y), solidColor,
            ImVec2(s3x, s3y), solidColor,
            ImVec2(s4x, s4y), fadeColor);
        DrawGradientTriangle(dl,
            ImVec2(s3x, s3y), solidColor,
            ImVec2(s5x, s5y), fadeColor,
            ImVec2(s4x, s4y), fadeColor);
    }

    // --- Outline: left and right edges only ---
    // Bottom edge is at ground level (implicit), top fades out.
    // Only the vertical sides communicate width and angle clearly.
    if (v0 && v4) dl->AddLine(ImVec2(s0x, s0y), ImVec2(s4x, s4y), lineColor, 2.0f); // Left edge
    if (v1 && v5) dl->AddLine(ImVec2(s1x, s1y), ImVec2(s5x, s5y), lineColor, 2.0f); // Right edge
    if (v0 && v1) dl->AddLine(ImVec2(s0x, s0y), ImVec2(s1x, s1y), lineColor, 2.0f); // Bottom edge
}

// ---------------------------------------------------------------------------
// RenderZones
// ---------------------------------------------------------------------------
// Entry point called every frame from AddonRender(). Iterates the active
// route and dispatches each checkpoint to the right shape renderer.
//
// Filtering rules:
//   • Hidden when the in-game world map is open (zones would clutter the map UI).
//   • MapChange triggers are never drawn (they have no spatial zone to show).
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

    // Map ID and camera position come from GS, populated each frame from
    // whichever source is active (RTAPI or Mumble).
    unsigned int currMapID = GS.MapID;

    auto shouldRender = [&](const RoutePoint& p) -> bool
    {
        if (!RoutePointIsSet(p))                          return false;
        if (p.TriggerType == ETriggerType::MapChange)     return false;
        if (p.MapID == 0)                                 return true;
        return currMapID == p.MapID;
    };

    auto renderPoint = [&](const RoutePoint& p, float r, float g, float b, float dbgOffset)
    {
        if (!shouldRender(p)) return;
    
        // Camera distance to zone centre for global fade
        float fdx = GS.CameraX - p.X;
        float fdy = GS.CameraY - p.Y;
        float fdz = GS.CameraZ - p.Z;
        float fdist = std::sqrt(fdx*fdx + fdy*fdy + fdz*fdz);
        float distAlpha = std::clamp(1.0f - (fdist - ZoneFadeStart) / (ZoneFadeEnd - ZoneFadeStart), 0.0f, 1.0f);
        if (distAlpha <= 0.0f) return;
    
        if (p.TriggerType == ETriggerType::Plane)
            RenderZonePlane(p, r, g, b, distAlpha);
        else
            RenderZoneCircle(p, r, g, b, dbgOffset, distAlpha);
    };

    Checkpoint* start = GetStart(CurrentRoute);
    Checkpoint* goal  = GetGoal(CurrentRoute);

    if (start) renderPoint(start->Point, 0.2f, 1.0f, 0.2f, 0.0f);
    if (goal)  renderPoint(goal->Point,  0.2f, 0.5f, 1.0f, 80.0f);

    int dbgIdx = 0;
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const Checkpoint& cp = CurrentRoute.Checkpoints[i];
        if (cp.IsStart || cp.IsGoal) continue;
        renderPoint(cp.Point, 1.0f, 1.0f, 1.0f, 160.0f + dbgIdx * 80.0f);
        dbgIdx++;
    }
}