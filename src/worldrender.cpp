// worldrender.cpp
#include "worldrender.h"
#include "shared.h"
#include "imgui.h"
#include <algorithm>
#include <cmath>
#include <cstdio>

static bool WorldToScreen(float wx, float wy, float wz, float& sx, float& sy)
{
    Vector3 camPos   = MumbleLink->CameraPosition;
    Vector3 camFront = MumbleLink->CameraFront;

    float flen = std::sqrt(camFront.X*camFront.X + camFront.Y*camFront.Y + camFront.Z*camFront.Z);
    if (flen < 0.0001f) return false;
    float fx = camFront.X / flen;
    float fy = camFront.Y / flen;
    float fz = camFront.Z / flen;

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

    float tx = fy*rz - fz*ry;
    float ty = fz*rx - fx*rz;
    float tz = fx*ry - fy*rx;

    float dx = wx - camPos.X;
    float dy = wy - camPos.Y;
    float dz = wz - camPos.Z;

    float vx =  rx*dx + ry*dy + rz*dz;
    float vy =  tx*dx + ty*dy + tz*dz;
    float vz =  fx*dx + fy*dy + fz*dz;

    if (vz <= 0.01f) return false;

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

static bool RoutePointIsSet(const RoutePoint& point)
{
    return point.X != 0.0f || point.Y != 0.0f || point.Z != 0.0f;
}

void RenderZoneCircle(const RoutePoint& point, float r, float g, float b, float debugOffsetY)
{
    if (!MumbleLink) return;

    ImDrawList* dl    = ImGui::GetForegroundDrawList();

    // Fade out rings when camera is close to the center
    float camDx    = MumbleLink->CameraPosition.X - point.X;
    float camDy    = MumbleLink->CameraPosition.Y - point.Y;
    float camDz    = MumbleLink->CameraPosition.Z - point.Z;
    float camDist  = std::sqrt(camDx*camDx + camDy*camDy + camDz*camDz);
    float fadeStart = point.Radius * 1.5f;
    float fadeEnd   = point.Radius * 0.8f;
    float alpha     = std::clamp((camDist - fadeEnd) / (fadeStart - fadeEnd), 0.0f, 1.0f);

    ImU32 color = IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), (int)(200 * alpha));

    const int steps = 64;

    // Draw two rings of which one is acting like a billboard.
    // Each ring gets its own prev state so rings don't connect to each other.
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

            if (valid && prevValid)
                dl->AddLine(ImVec2(prevSx, prevSy), ImVec2(sx, sy), color, 2.0f);

            prevSx    = sx;
            prevSy    = sy;
            prevValid = valid;
        }
    };

    // Horizontal ring (XZ plane)
    DrawRing([&](float a) { return std::make_tuple(point.X + std::cos(a) * point.Radius, point.Y, point.Z + std::sin(a) * point.Radius); });

    // Vertical ring always facing the camera (rotates around Y axis only)
    float dx = MumbleLink->CameraPosition.X - point.X;
    float dz = MumbleLink->CameraPosition.Z - point.Z;
    float dlen = std::sqrt(dx*dx + dz*dz);
    if (dlen > 0.0001f) { dx /= dlen; dz /= dlen; }
    DrawRing([&](float a) { return std::make_tuple(
        point.X + std::cos(a) * (-dz) * point.Radius,
        point.Y + std::sin(a) * point.Radius,
        point.Z + std::cos(a) * dx * point.Radius
    ); });

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

        if (valid)
            dl->AddCircleFilled(ImVec2(sx, sy), 10.0f,
                IM_COL32((int)(r*255),(int)(g*255),(int)(b*255),255));
    }
}

void RenderZonePlane(const RoutePoint& point, float r, float g, float b)
{
    if (!MumbleLink) return;

    ImDrawList* dl    = ImGui::GetForegroundDrawList();
    ImU32       color = IM_COL32((int)(r*255), (int)(g*255), (int)(b*255), 200);

    float angleRad = point.PlaneAngle * 3.14159265f / 180.0f;

    // Plane direction vector (along the plane)
    float px = -std::sin(angleRad);
    float pz =  std::cos(angleRad);

    float halfWidth = point.PlaneWidth * 0.5f;

    // 4 corners of the vertical plane
    // Bottom left, bottom right, top right, top left
    // Use a reasonable height range around the point
    float yBottom = point.Y - 2.0f;
    float yTop    = point.Y + 3.0f;

    float wx0 = point.X - px * halfWidth;
    float wz0 = point.Z - pz * halfWidth;
    float wx1 = point.X + px * halfWidth;
    float wz1 = point.Z + pz * halfWidth;

    float s0x, s0y, s1x, s1y, s2x, s2y, s3x, s3y;
    bool v0 = WorldToScreen(wx0, yBottom, wz0, s0x, s0y);
    bool v1 = WorldToScreen(wx1, yBottom, wz1, s1x, s1y);
    bool v2 = WorldToScreen(wx1, yTop,    wz1, s2x, s2y);
    bool v3 = WorldToScreen(wx0, yTop,    wz0, s3x, s3y);

    // Draw edges
    if (v0 && v1) dl->AddLine(ImVec2(s0x, s0y), ImVec2(s1x, s1y), color, 2.0f); // bottom
    if (v2 && v3) dl->AddLine(ImVec2(s2x, s2y), ImVec2(s3x, s3y), color, 2.0f); // top
    if (v0 && v3) dl->AddLine(ImVec2(s0x, s0y), ImVec2(s3x, s3y), color, 2.0f); // left
    if (v1 && v2) dl->AddLine(ImVec2(s1x, s1y), ImVec2(s2x, s2y), color, 2.0f); // right

    // Diagonal cross for clarity
    if (v0 && v2) dl->AddLine(ImVec2(s0x, s0y), ImVec2(s2x, s2y), color, 1.0f);
    if (v1 && v3) dl->AddLine(ImVec2(s1x, s1y), ImVec2(s3x, s3y), color, 1.0f);
}

void RenderZones()
{
    if (!MumbleLink || !ShowZones) return;
    if (MumbleLink->Context.IsMapOpen) return;

    unsigned int currMapID = MumbleLink->Context.MapID;

    auto shouldRender = [&](const RoutePoint& p) -> bool {
        if (!RoutePointIsSet(p)) return false;
        if (p.TriggerType == ETriggerType::MapChange) return false;
        if (p.MapID == 0) return true;
        return currMapID == p.MapID;
    };

    auto renderPoint = [&](const RoutePoint& p, float r, float g, float b, float dbgOffset) {
        if (!shouldRender(p)) return;
        if (p.TriggerType == ETriggerType::Plane)
            RenderZonePlane(p, r, g, b);
        else
            RenderZoneCircle(p, r, g, b, dbgOffset);
    };

    Checkpoint* start = GetStart(CurrentRoute);
    Checkpoint* goal  = GetGoal(CurrentRoute);

    if (start) renderPoint(start->Point, 0.2f, 1.0f, 0.2f, 0.0f);
    if (goal)  renderPoint(goal->Point,  0.2f, 0.5f, 1.0f, 80.0f);

    int dbgIdx = 0;
    for (int i = 0; i < (int)CurrentRoute.Checkpoints.size(); i++)
    {
        const Checkpoint& cp = CurrentRoute.Checkpoints[i];
        if (cp.IsStart || cp.IsGoal) continue; // already rendered above
        renderPoint(cp.Point, 1.0f, 1.0f, 1.0f, 160.0f + dbgIdx * 80.0f);
        dbgIdx++;
    }
}