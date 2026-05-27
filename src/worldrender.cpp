// worldrender.cpp
// Draws checkpoint zone overlays directly onto the game world using ImGui's
// foreground draw list — the same technique used by most GW2 overlay addons.
//
// Two visual shapes are supported:
//   Circle zone — two rings (one horizontal, one vertical billboard) that
//                 outline a sphere trigger in 3D space.
//   Plane zone  — a rectangle with a diagonal cross showing where and how
//                 wide a plane trigger is.
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
#include <cstdio>

// ---------------------------------------------------------------------------
// WorldToScreen  (file-private)
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
static bool WorldToScreen(float wx, float wy, float wz, float& sx, float& sy)
{
    Vector3 camPos   = MumbleLink->CameraPosition;
    Vector3 camFront = MumbleLink->CameraFront;

    // Step 1 — normalise the forward vector.
    float flen = std::sqrt(camFront.X*camFront.X + camFront.Y*camFront.Y + camFront.Z*camFront.Z);
    if (flen < 0.0001f) return false; // Degenerate camera direction — skip
    float fx = camFront.X / flen;
    float fy = camFront.Y / flen;
    float fz = camFront.Z / flen;

    // Step 2 — derive the right vector (forward × world-up).
    // When the camera is nearly vertical (|fy| > 0.999) the cross product
    // degenerates, so fall back to world-right (1,0,0).
    float rx, ry, rz;
    if (std::abs(fy) > 0.999f)
    {
        rx = 1.0f; ry = 0.0f; rz = 0.0f;
    }
    else
    {
        // Cross product of (fx,fy,fz) × (0,1,0) = (fz, 0, -fx), then normalise.
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

    // Step 4 — transform the world offset into camera space by projecting
    // onto the three camera basis vectors.
    //   vx = right component   (positive = screen right)
    //   vy = up component      (positive = screen up)
    //   vz = depth component   (positive = in front of camera)
    float dx = wx - camPos.X;
    float dy = wy - camPos.Y;
    float dz = wz - camPos.Z;

    float vx =  rx*dx + ry*dy + rz*dz;
    float vy =  tx*dx + ty*dy + tz*dz;
    float vz =  fx*dx + fy*dy + fz*dz;

    // Reject anything on or behind the near clip plane.
    if (vz <= 0.01f) return false;

    // Step 5 — perspective projection.
    // f = focal length derived from the vertical FOV.
    // aspect corrects the horizontal field of view for non-square screens.
    ImGuiIO& io  = ImGui::GetIO();
    float aspect = io.DisplaySize.x / io.DisplaySize.y;
    float f      = 1.0f / std::tan(CameraFOV * 0.5f);

    // NDC coordinates in [-1, 1]; Y is negated because screen Y increases downward.
    float px =  vx * (f / aspect) / vz;
    float py = -vy * f / vz;

    // Remap from [-1,1] NDC to pixel coordinates.
    sx = (px * 0.5f + 0.5f) * io.DisplaySize.x;
    sy = (py * 0.5f + 0.5f) * io.DisplaySize.y;

    // Discard points that project wildly off-screen (avoids drawing huge lines
    // across the screen when a point is just barely in front of the camera).
    if (sx < -10000 || sx > 10000 || sy < -10000 || sy > 10000) return false;

    return true;
}

// ---------------------------------------------------------------------------
// RoutePointIsSet  (file-private helper)
// ---------------------------------------------------------------------------
// Returns false for a default-constructed RoutePoint (all zeros) so we don't
// draw a circle at the world origin for checkpoints that haven't been placed yet.
// ---------------------------------------------------------------------------
static bool RoutePointIsSet(const RoutePoint& point)
{
    return point.X != 0.0f || point.Y != 0.0f || point.Z != 0.0f;
}

// ---------------------------------------------------------------------------
// RenderZoneCircle
// ---------------------------------------------------------------------------
// Draws two rings around a circle/sphere trigger zone using line segments
// projected through WorldToScreen:
//
//   Horizontal ring — lies flat on the XZ plane at the checkpoint's Y height.
//                     Shows the footprint of the trigger from above.
//
//   Vertical billboard ring — always faces the camera by rotating around the
//                     world Y axis only.  Together with the horizontal ring
//                     it gives a clear 3D sense of the sphere's extent from
//                     any camera angle.
//
// Both rings fade out as the camera moves inside the trigger radius to avoid
// a distracting full-screen circle when the player is standing in the zone.
//
// debugOffsetY staggers multiple debug text blocks vertically so they don't
// overlap when several checkpoints are visible at the same time.
// ---------------------------------------------------------------------------
void RenderZoneCircle(const RoutePoint& point, float r, float g, float b, float debugOffsetY)
{
    if (!MumbleLink) return;

    ImDrawList* dl = ImGui::GetForegroundDrawList();

    // --- Fade when camera is close to the zone centre ---
    // Alpha is 1 when the camera is outside 1.5× the radius,
    // linearly fades to 0 at 0.8× the radius (inside the zone).
    float camDx   = MumbleLink->CameraPosition.X - point.X;
    float camDy   = MumbleLink->CameraPosition.Y - point.Y;
    float camDz   = MumbleLink->CameraPosition.Z - point.Z;
    float camDist = std::sqrt(camDx*camDx + camDy*camDy + camDz*camDz);
    float fadeStart = point.Radius * 1.5f;
    float fadeEnd   = point.Radius * 0.8f;
    float alpha = std::clamp((camDist - fadeEnd) / (fadeStart - fadeEnd), 0.0f, 1.0f);

    ImU32 color = IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), (int)(200 * alpha));

    // 64 line segments per ring — smooth enough to look circular at typical
    // game resolutions without being expensive to project.
    const int steps = 64;

    // Generic ring drawing lambda: takes a function that returns (wx, wy, wz)
    // for a given angle, then projects and connects each point with a line.
    // Each ring gets independent prev state so rings don't connect to each other.
    auto DrawRing = [&](auto getPoint)
    {
        float prevSx = 0, prevSy = 0;
        bool  prevValid = false;

        for (int i = 0; i <= steps; i++)
        {
            float angle = (float)i / steps * 2.0f * 3.14159265f;
            auto [wx, wy, wz] = getPoint(angle);

            float sx, sy;
            bool valid = WorldToScreen(wx, wy, wz, sx, sy);

            // Only draw a segment if both endpoints projected successfully.
            // This naturally handles the ring going partially behind the camera.
            if (valid && prevValid)
                dl->AddLine(ImVec2(prevSx, prevSy), ImVec2(sx, sy), color, 2.0f);

            prevSx    = sx;
            prevSy    = sy;
            prevValid = valid;
        }
    };

    // Horizontal ring — flat on the XZ plane at point.Y
    DrawRing([&](float a) {
        return std::make_tuple(
            point.X + std::cos(a) * point.Radius,
            point.Y,
            point.Z + std::sin(a) * point.Radius);
    });

    // Vertical billboard ring — always rotated to face the camera on the Y axis.
    // The ring is drawn in the plane that contains the world Y axis and the
    // vector from the zone centre to the camera.
    float dx   = MumbleLink->CameraPosition.X - point.X;
    float dz   = MumbleLink->CameraPosition.Z - point.Z;
    float dlen = std::sqrt(dx*dx + dz*dz);
    if (dlen > 0.0001f) { dx /= dlen; dz /= dlen; } // Normalise the facing direction
    DrawRing([&](float a) {
        return std::make_tuple(
            point.X + std::cos(a) * (-dz) * point.Radius, // Perpendicular on X
            point.Y + std::sin(a) * point.Radius,          // Vertical component
            point.Z + std::cos(a) *  dx   * point.Radius); // Perpendicular on Z
    });

    // --- Debug overlay (only shown when ShowDebug is on) ---
    // Draws the centre dot and coordinate text so route authors can verify
    // that the checkpoint is placed at the right world position.
    if (ShowDebug)
    {
        float sx, sy;
        bool valid = WorldToScreen(point.X, point.Y, point.Z, sx, sy);

        float lineH = ImGui::GetTextLineHeight() + 2.0f;
        float dbgX  = 400.0f;
        float dbgY  = 100.0f + debugOffsetY;

        char buf[128];
        snprintf(buf, sizeof(buf), "CENTER: %s", valid ? "VALID" : "INVALID");
        dl->AddText(ImVec2(dbgX, dbgY), IM_COL32(255,255,0,255), buf);

        snprintf(buf, sizeof(buf), "pos: %.1f %.1f %.1f", point.X, point.Y, point.Z);
        dl->AddText(ImVec2(dbgX, dbgY + lineH), IM_COL32(255,255,0,255), buf);

        snprintf(buf, sizeof(buf), "cam: %.1f %.1f %.1f",
            MumbleLink->CameraPosition.X,
            MumbleLink->CameraPosition.Y,
            MumbleLink->CameraPosition.Z);
        dl->AddText(ImVec2(dbgX, dbgY + lineH*2), IM_COL32(255,255,0,255), buf);

        snprintf(buf, sizeof(buf), "screen: %.1f %.1f", sx, sy);
        dl->AddText(ImVec2(dbgX, dbgY + lineH*3), IM_COL32(255,255,0,255), buf);

        // Draw a filled dot at the projected centre position
        if (valid)
            dl->AddCircleFilled(ImVec2(sx, sy), 10.0f,
                IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 255));
    }
}

// ---------------------------------------------------------------------------
// RenderZonePlane
// ---------------------------------------------------------------------------
// Draws a rectangle with a diagonal cross representing a Plane trigger.
//
// The rectangle spans the full PlaneWidth horizontally and a fixed 5 m
// vertically (2 m below the checkpoint Y to 3 m above it — roughly player
// height range).  The diagonal cross makes the facing direction and width
// immediately readable even at a distance.
//
// Each of the four corners is projected independently; a line segment is only
// drawn if both of its endpoints are in front of the camera.
// ---------------------------------------------------------------------------
void RenderZonePlane(const RoutePoint& point, float r, float g, float b)
{
    if (!MumbleLink) return;

    ImDrawList* dl    = ImGui::GetForegroundDrawList();
    ImU32       color = IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 200);

    // Build the along-plane direction vector from PlaneAngle.
    float angleRad = point.PlaneAngle * 3.14159265f / 180.0f;
    float px = -std::sin(angleRad); // Along-plane X component
    float pz =  std::cos(angleRad); // Along-plane Z component

    float halfWidth = point.PlaneWidth * 0.5f;

    // Four corners of the plane rectangle in world space:
    //   (wx0, yBottom/yTop, wz0) = left edge
    //   (wx1, yBottom/yTop, wz1) = right edge
    float yBottom = point.Y - 2.0f; // 2 m below checkpoint centre
    float yTop    = point.Y + 3.0f; // 3 m above checkpoint centre

    float wx0 = point.X - px * halfWidth; // Left endpoint
    float wz0 = point.Z - pz * halfWidth;
    float wx1 = point.X + px * halfWidth; // Right endpoint
    float wz1 = point.Z + pz * halfWidth;

    // Project all four corners; draw each edge only when both endpoints are valid.
    float s0x, s0y, s1x, s1y, s2x, s2y, s3x, s3y;
    bool v0 = WorldToScreen(wx0, yBottom, wz0, s0x, s0y); // Bottom-left
    bool v1 = WorldToScreen(wx1, yBottom, wz1, s1x, s1y); // Bottom-right
    bool v2 = WorldToScreen(wx1, yTop,    wz1, s2x, s2y); // Top-right
    bool v3 = WorldToScreen(wx0, yTop,    wz0, s3x, s3y); // Top-left

    // Outline edges
    if (v0 && v1) dl->AddLine(ImVec2(s0x, s0y), ImVec2(s1x, s1y), color, 2.0f); // Bottom
    if (v2 && v3) dl->AddLine(ImVec2(s2x, s2y), ImVec2(s3x, s3y), color, 2.0f); // Top
    if (v0 && v3) dl->AddLine(ImVec2(s0x, s0y), ImVec2(s3x, s3y), color, 2.0f); // Left
    if (v1 && v2) dl->AddLine(ImVec2(s1x, s1y), ImVec2(s2x, s2y), color, 2.0f); // Right

    // Diagonal cross — makes the plane's orientation readable at a glance
    if (v0 && v2) dl->AddLine(ImVec2(s0x, s0y), ImVec2(s2x, s2y), color, 1.0f);
    if (v1 && v3) dl->AddLine(ImVec2(s1x, s1y), ImVec2(s3x, s3y), color, 1.0f);
}

// ---------------------------------------------------------------------------
// RenderZones
// ---------------------------------------------------------------------------
// Entry point called every frame from AddonRender().  Iterates the active
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
    if (!MumbleLink || !ShowZones) return;

    // Don't draw zones while the world map is open — they'd appear on top of the map UI.
    if (MumbleLink->Context.IsMapOpen) return;

    unsigned int currMapID = MumbleLink->Context.MapID;

    // Returns true if a point should be drawn this frame.
    auto shouldRender = [&](const RoutePoint& p) -> bool
    {
        if (!RoutePointIsSet(p))                          return false; // Unplaced checkpoint
        if (p.TriggerType == ETriggerType::MapChange)     return false; // No spatial zone to show
        if (p.MapID == 0)                                 return true;  // Any map
        return currMapID == p.MapID;                                    // Specific map only
    };

    // Dispatches to circle or plane renderer based on trigger type.
    auto renderPoint = [&](const RoutePoint& p, float r, float g, float b, float dbgOffset)
    {
        if (!shouldRender(p)) return;
        if (p.TriggerType == ETriggerType::Plane)
            RenderZonePlane(p, r, g, b);
        else
            RenderZoneCircle(p, r, g, b, dbgOffset);
    };

    Checkpoint* start = GetStart(CurrentRoute);
    Checkpoint* goal  = GetGoal(CurrentRoute);

    // Start = green, goal = blue, drawn before intermediates
    if (start) renderPoint(start->Point, 0.2f, 1.0f, 0.2f, 0.0f);
    if (goal)  renderPoint(goal->Point,  0.2f, 0.5f, 1.0f, 80.0f);

    // Intermediate checkpoints = white; debug offset staggered per checkpoint
    // so their debug text blocks don't overlap when multiple are on screen.
    int dbgIdx = 0;
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const Checkpoint& cp = CurrentRoute.Checkpoints[i];
        if (cp.IsStart || cp.IsGoal) continue; // Already rendered above
        renderPoint(cp.Point, 1.0f, 1.0f, 1.0f, 160.0f + dbgIdx * 80.0f);
        dbgIdx++;
    }
}