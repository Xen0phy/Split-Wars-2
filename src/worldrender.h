// worldrender.h
// Declares the world-space overlay rendering functions.
//
// These functions draw checkpoint zone indicators directly onto the game
// world each frame using ImGui's foreground draw list.  They are called
// from AddonRender() and are only active when ShowZones is true and the
// in-game world map is closed.

#pragma once

#include "route.h"

// ---------------------------------------------------------------------------
// WorldToScreen
// ---------------------------------------------------------------------------
// Projects a world-space point (wx, wy, wz) onto screen space (sx, sy).
// Returns false when the point is behind the camera or far off-screen.
// ---------------------------------------------------------------------------
bool WorldToScreen(float wx, float wy, float wz, float& sx, float& sy);

// ---------------------------------------------------------------------------
// RenderZoneCircle
// ---------------------------------------------------------------------------
// Draws two projected rings (one horizontal, one vertical billboard) around
// a sphere trigger zone.  Used for Circle, CircleInteract, and CombatArena
// trigger types.
//
//   point          — the RoutePoint defining the zone centre and radius.
//   r, g, b        — ring colour (0.0–1.0 per channel); alpha is computed
//                    internally and fades out when the camera is inside the zone.
//   debugOffsetY   — vertical pixel offset for the debug text block so
//                    multiple visible zones don't overlap their text.
//                    Defaults to 0; only meaningful when ShowDebug is on.
// ---------------------------------------------------------------------------
void RenderZoneCircle(const RoutePoint& point, float r, float g, float b);
void RenderZonePlane(const RoutePoint& point, float r, float g, float b);

// ---------------------------------------------------------------------------
// RenderZoneMap
// ---------------------------------------------------------------------------
// Draws a screen-space quarter-circle dot field in the upper-left corner of
// the screen for MapChange trigger zones.  Dots fade toward the arc edge and
// disappear when the mouse cursor is within 150 px.
// ---------------------------------------------------------------------------
void RenderZoneMap(const RoutePoint& point, float r, float g, float b);

// ---------------------------------------------------------------------------
// RenderZones
// ---------------------------------------------------------------------------
// Main entry point — iterates the active route and draws every checkpoint
// zone that belongs to the current map.
//
// Colour convention:
//   Green (0.2, 1.0, 0.2) — start checkpoint
//   Blue  (0.2, 0.5, 1.0) — goal checkpoint
//   White (1.0, 1.0, 1.0) — intermediate checkpoints
//
// MapChange checkpoints and unplaced checkpoints (position all zeros) are
// skipped — they have no meaningful zone to draw.
// ---------------------------------------------------------------------------
void RenderZones();