// route.h
// Core data types and spatial query functions for the route system.
//
// A Route is an ordered list of Checkpoints.  Each Checkpoint wraps a
// RoutePoint (the spatial trigger) plus a display name and start/goal flags.
// Exactly one checkpoint should be flagged IsStart and one IsGoal for the
// timer logic in AddonRender() to work correctly.
//
// COORDINATE SYSTEM NOTE:
// All position values (X, Y, Z, Radius, PlaneWidth) are in the units that
// MumbleLink exposes — metres.  GW2 internally uses inches, but the Mumble
// API converts to metres before writing to shared memory, so everything here
// works in metres.  Keep this in mind when setting Radius values: a radius
// of 5.0 means 5 metres, not 5 inches.

#pragma once

#include "Mumble.h"
#include <vector>
#include <chrono>

// ---------------------------------------------------------------------------
// ETriggerType
// ---------------------------------------------------------------------------
// Determines what geometric or game-event condition fires a checkpoint.
// The underlying type is unsigned char so it serialises compactly to JSON.
//
//   Circle         — fires while the player is inside a sphere (Radius).
//   Plane          — fires when the player's movement crosses a finite plane
//                    (PlaneAngle + PlaneWidth).
//   MapChange      — fires when the player leaves a specific map (MapID).
//   CircleInteract — like Circle, but also requires the Interact key to be
//                    pressed while inside the sphere.
//   CombatArena    — fires based on entering/leaving combat inside a sphere;
//                    uses a 2-second grace period to filter brief combat drops.
//   AllCheckpoints — goal-only type; fires when every other non-start/non-goal
//                    checkpoint has been triggered at least once.
// ---------------------------------------------------------------------------
enum class ETriggerType : unsigned char
{
    Circle          = 0,
    Plane           = 1,
    MapChange       = 2,
    CircleInteract  = 3,
    CombatArena     = 4,
    AllCheckpoints  = 5
};

// ---------------------------------------------------------------------------
// ECombatState
// ---------------------------------------------------------------------------
// The internal state machine used by CombatTriggerState (below).
//
//   Armed        — player is currently in combat inside the trigger zone.
//   GracePending — player dropped combat; waiting to see if it resumes before
//                  the 2-second grace period expires.
//   Finished     — the combat segment has fully resolved and the split fired.
//                  (Stored separately in CombatTriggerState::finished rather
//                  than as a third enum value so the active/finished flags
//                  remain easy to check independently.)
// ---------------------------------------------------------------------------
enum class ECombatState : unsigned char
{
    Armed,
    GracePending,
    Finished
};

// ---------------------------------------------------------------------------
// CombatTriggerState
// ---------------------------------------------------------------------------
// Per-checkpoint runtime state for CombatArena triggers.  One instance exists
// for the start point (CombatStart), one for the goal (CombatGoal), and one
// per intermediate checkpoint in the CombatCheckpoints vector.
//
//   active     — true once the player has entered combat inside the zone.
//   state      — current position in the Armed → GracePending state machine.
//   dropTime   — elapsed run time (seconds) at which combat was first dropped;
//                used to back-date the split to the actual combat-end moment
//                rather than when the grace period expired.
//   graceStart — wall-clock time when the grace period began; compared against
//                steady_clock::now() each frame to check for expiry.
//   finished   — true once the segment has resolved and the split has fired;
//                prevents the same zone from triggering a second split.
// ---------------------------------------------------------------------------
struct CombatTriggerState
{
    bool                                  active     = false;
    ECombatState                          state      = ECombatState::Armed;
    double                                dropTime   = 0.0;
    std::chrono::steady_clock::time_point graceStart;
    bool                                  finished   = false;
};

// ---------------------------------------------------------------------------
// RoutePoint
// ---------------------------------------------------------------------------
// The spatial definition of a single trigger.  All position and size values
// are in metres (see coordinate system note at the top of this file).
//
//   MapID       — the GW2 map the trigger is scoped to.  0 = any map.
//   X, Y, Z     — world-space centre of the trigger (metres).
//   Radius      — sphere radius for Circle / CircleInteract / CombatArena.
//                 Default 5.0 m.
//   TriggerType — which geometry / game event activates this point.
//   PlaneWidth  — total width of the active segment of a Plane trigger (metres).
//                 Only crossings within ±PlaneWidth/2 of the centre count.
//                 Default 10.0 m.
//   PlaneAngle  — compass heading the plane faces, in degrees.
//                 0° = north (+Z axis), 90° = east (+X axis).
// ---------------------------------------------------------------------------
struct RoutePoint
{
    unsigned int    MapID               = 0;
    float           X                   = 0.0f;
    float           Y                   = 0.0f;
    float           Z                   = 0.0f;
    float           Radius              = 5.0f;
    ETriggerType    TriggerType         = ETriggerType::Circle;
    float           PlaneWidth          = 10.0f;
    float           PlaneAngle          = 0.0f;
    int     DotSphereCount      = 0; // Display only: >0 renders as a dot sphere with this many dots
};

// ---------------------------------------------------------------------------
// Checkpoint
// ---------------------------------------------------------------------------
// A named waypoint in the route.  Wraps a RoutePoint with a display name and
// two flags that determine the checkpoint's role in the run:
//
//   IsStart — the timer starts (or resets + starts) when this point fires.
//             Only one checkpoint per route should have this set.
//   IsGoal  — the timer stops and the run is recorded when this point fires.
//             Only one checkpoint per route should have this set.
//
// All other checkpoints (neither start nor goal) record intermediate splits.
// ---------------------------------------------------------------------------
struct Checkpoint
{
    RoutePoint  Point;
    char        Name[64] = {}; // Display name shown in the timer and history
    bool        IsStart  = false;
    bool        IsGoal   = false;
};

// ---------------------------------------------------------------------------
// Route
// ---------------------------------------------------------------------------
// The full route: an ordered list of checkpoints and an IsValid flag.
// IsValid must be set to true (via "Activate Route" in the config window)
// before AddonRender() will evaluate any triggers.  This prevents a partially
// edited route from accidentally starting the timer mid-edit.
// ---------------------------------------------------------------------------
struct Route
{
    std::vector<Checkpoint> Checkpoints;
    bool                    IsValid = false;
};

// ---------------------------------------------------------------------------
// GetStart / GetGoal  (inline helpers)
// ---------------------------------------------------------------------------
// Linear scans to find the designated start or goal checkpoint by flag.
// Returns nullptr if no checkpoint has the flag set (e.g. an incomplete route).
// ---------------------------------------------------------------------------
inline Checkpoint* GetStart(Route& route)
{
    for (auto& cp : route.Checkpoints)
        if (cp.IsStart) return &cp;
    return nullptr;
}

inline Checkpoint* GetGoal(Route& route)
{
    for (auto& cp : route.Checkpoints)
        if (cp.IsGoal) return &cp;
    return nullptr;
}

// ---------------------------------------------------------------------------
// Spatial query functions (implemented in route.cpp)
// ---------------------------------------------------------------------------

// Returns the 3D distance in metres between the player and a route point.
float DistanceTo(const Vector3& playerPos, const RoutePoint& point);

// Returns true when the player is within the point's Radius (Circle trigger).
bool IsWithinRange(const Vector3& playerPos, const RoutePoint& point);

// Returns true when the player's movement between prevPos and currPos crossed
// the finite trigger plane defined by the point's angle and width.
bool HasCrossedPlane(const Vector3& prevPos, const Vector3& currPos, const RoutePoint& point);