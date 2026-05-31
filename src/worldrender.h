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
// Draws a sphere of projected dots around a circle trigger zone using a
// Fibonacci/golden-angle distribution for even coverage.  Used for Circle,
// CircleInteract, and CombatArena trigger types.
//
//   point          — the RoutePoint defining the zone centre, radius, dot
//                    count, and band parameters (center/up/down in degrees).
//   r, g, b        — dot colour (0.0–1.0 per channel).
//
// Dot alpha is the product of band edge falloff, per-dot distance fade from
// the player through ZoneFadeStart/ZoneFadeEnd, and occlusion fade via
// ApplyOcclusion.
//
// Trigger-specific behaviour:
//   CombatArena    — radius pulses with a heartbeat (lub-dub) animation
//                    while the player is out of combat.
//   CircleInteract — a rotating gap sweeps around the sphere with softened
//                    feather edges, giving a beckoning visual cue.
// ---------------------------------------------------------------------------
void RenderZoneCircle(const RoutePoint& point, float r, float g, float b);

// ---------------------------------------------------------------------------
// RenderZonePlane
// ---------------------------------------------------------------------------
// Draws a projected dot field across an infinite plane trigger zone.
// Used for Plane trigger types.
//
//   point          — the RoutePoint defining the plane origin, normal, and
//                    radius width.
//   r, g, b        — dot colour (0.0–1.0 per channel); alpha is computed
//                    internally and fades out when the camera is close to
//                    the plane.
// ---------------------------------------------------------------------------
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