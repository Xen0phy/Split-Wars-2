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
//
// This rebuilds the camera basis (front/right/up vectors, FOV projection
// factor) on every call, which is fine for the occasional debug-UI lookup
// this is used for elsewhere, but far too costly to call per-dot. The
// per-frame dot renderers use CameraBasis + ProjectWithBasis() instead (see
// below) so the basis is only built once per frame no matter how many dots
// are drawn.
// ---------------------------------------------------------------------------
bool WorldToScreen(float wx, float wy, float wz, float& sx, float& sy);

// ---------------------------------------------------------------------------
// CameraBasis
// ---------------------------------------------------------------------------
// Camera-derived values that are constant for an entire frame: the
// normalised forward/right/up vectors, the perspective factor derived from
// FOV, and display size/aspect. Built once per frame by BuildCameraBasis()
// and reused for every dot projected that frame via ProjectWithBasis(),
// instead of recomputing (2 sqrt calls + a tan() call + cross products)
// on every single WorldToScreen() call as the naive per-point version does.
// ---------------------------------------------------------------------------
struct CameraBasis
{
    bool  valid = false;
    float camX = 0, camY = 0, camZ = 0;
    float rx = 0, ry = 0, rz = 0; // right
    float tx = 0, ty = 0, tz = 0; // up
    float fx = 0, fy = 0, fz = 0; // forward (normalised)
    float f      = 1.0f;          // 1 / tan(fov * 0.5)
    float aspect = 1.0f;
    float dispW  = 1.0f, dispH = 1.0f;
};

// Builds a CameraBasis from the current MumbleLink/RTAPI camera state.
// Call once per frame, not per dot.
CameraBasis BuildCameraBasis();

// Fast per-point projection against an already-built CameraBasis. This is
// what WorldToScreen() does internally, minus the basis reconstruction —
// use this in any per-dot hot loop.
bool ProjectWithBasis(const CameraBasis& basis, float wx, float wy, float wz, float& sx, float& sy);

// ---------------------------------------------------------------------------
// OcclusionState
// ---------------------------------------------------------------------------
// Player-occlusion info, computed once per frame (via CalcOcclusionState)
// and passed into both zone renderers so the (basis-dependent) player
// projection and distance math isn't repeated per zone.
// ---------------------------------------------------------------------------
struct OcclusionState
{
    bool  playerOnScreen = false;
    float playerSx = 0, playerSy = 0;
    float occludeRadius = 0;
};

// Computes player occlusion state for this frame using an already-built
// CameraBasis. Call once per frame in RenderZones(), not once per zone.
OcclusionState CalcOcclusionState(const CameraBasis& basis);

// ---------------------------------------------------------------------------
// RenderZoneCircle
// ---------------------------------------------------------------------------
// Draws a sphere of projected dots around a circle trigger zone using a
// Fibonacci/golden-angle distribution for even coverage.  Used for Circle,
// CircleInteract, and CombatArena trigger types.
//
//   point          — the RoutePoint defining the zone centre, radius, dot
//                    count, and band parameters (center/up/down in degrees).
//   r, g, b        — dot color (0.0–1.0 per channel).
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
//
// Camera-independent per-dot data (unit direction + band falloff + raw
// longitude) is cached per-RoutePoint and only regenerated when the dot
// count or band parameters change — see the sphere-point cache in the .cpp.
// basis/os are the once-per-frame values from BuildCameraBasis() and
// CalcOcclusionState(), threaded through so this function does no camera
// or occlusion setup work of its own.
// ---------------------------------------------------------------------------
void RenderZoneCircle(const RoutePoint& point, float r, float g, float b,
                      const CameraBasis& basis, const OcclusionState& os);

// ---------------------------------------------------------------------------
// RenderZonePlane
// ---------------------------------------------------------------------------
// Draws a projected dot field across a finite plane trigger zone.
// Used for Plane trigger types.
//
//   point          — the RoutePoint defining the plane origin, normal, and
//                    width and height.
//   r, g, b        — dot color (0.0–1.0 per channel); alpha is computed
//                    internally and fades out when the camera is close to
//                    the plane.
// ---------------------------------------------------------------------------
void RenderZonePlane(const RoutePoint& point, float r, float g, float b,
                     const CameraBasis& basis, const OcclusionState& os);

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
// Colors are read from the user-configurable ColorStart, ColorGoal, and
// ColorCheckpoint globals (set in addon_options.cpp).
//
// MapChange checkpoints and unplaced checkpoints (position all zeros) are
// skipped — they have no meaningful zone to draw.
// ---------------------------------------------------------------------------
void RenderZones();