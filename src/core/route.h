// route.h
// Core data types and spatial query functions for the route system.
//
// A Route is an ordered list of CheckpointStates.  Each CheckpointState holds
// both the spatial trigger config (RoutePoint, Name, IsStart, IsGoal) and all
// per-run runtime state for that checkpoint.
//
// Exactly one checkpoint should be flagged IsStart; one or more may be
// flagged IsGoal (multiple goals allow routes that diverge at a branch point).
//
// COORDINATE SYSTEM NOTE:
// All position values (X, Y, Z, RadiusWidth) are in the units that
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
//                    When IsStart is true, fires on LEAVING the sphere
//                    (enter resets the run; exit starts the timer).
//                    When IsStart is false, fires on ENTERING the sphere.
//   Plane          — fires when the player's movement crosses a finite plane
//                    (PlaneAngle + RadiusWidth).
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
    AllCheckpoints  = 5,
    NullCircle      = 6,  // Decorative only — renders like Circle but never triggers
    NullPlane       = 7,  // Decorative only — renders like Plane but never triggers
};

// ---------------------------------------------------------------------------
// ECombatState
// ---------------------------------------------------------------------------
// The internal state machine used by CombatTriggerState.
//
//   Armed        — player is currently in combat inside the trigger zone.
//   GracePending — player dropped combat; waiting to see if it resumes before
//                  the grace period expires.
// Whether the segment has fully resolved is tracked by CombatTriggerState::finished,
// not by a state value here, so Armed and GracePending are the only live states.
// ---------------------------------------------------------------------------
enum class ECombatState : unsigned char
{
    Armed,
    GracePending
};

// ---------------------------------------------------------------------------
// CombatTriggerState
// ---------------------------------------------------------------------------
// Per-checkpoint runtime state for CombatArena triggers.
//
//   active     — true once the player has entered combat inside the zone.
//   state      — current position in the Armed → GracePending state machine.
//   dropTime   — elapsed run time (seconds) at which combat was first dropped;
//                used to back-date the split to the actual combat-end moment
//                rather than when the grace period expired.
//   graceStart — wall-clock time when the grace period began; compared against
//                steady_clock::now() each frame to check for expiry.
//   graceAccum — accumulated seconds of grace that have elapsed while the
//                player was moving (Mumble path only). Grace only counts down
//                while moving, so this is compared against GraceDuration rather
//                than raw wall-clock elapsed.
//   finished   — true once the segment has resolved and the split has fired;
//                prevents the same zone from triggering a second split.
//   taintedPending — set when RTAPI detects the player is fully dead mid-segment.
//                    A __TAINTED__ split has been injected; we wait for IsAlive
//                    to become true again before firing a clean combat end.
// ---------------------------------------------------------------------------
struct CombatTriggerState
{
    bool                                  active         = false;
    ECombatState                          state          = ECombatState::Armed;
    double                                dropTime       = 0.0;
    std::chrono::steady_clock::time_point graceStart;
    double                                graceAccum     = 0.0;
    bool                                  finished       = false;
    bool                                  taintedPending = false;
};

// ---------------------------------------------------------------------------
// RoutePoint
// ---------------------------------------------------------------------------
// The spatial definition of a single trigger.  All position and size values
// are in metres (see coordinate system note at the top of this file).
//
//   MapID       — the GW2 map the trigger is scoped to.  0 = any map.
//   X, Y, Z     — world-space centre of the trigger (metres).
//   RadiusWidth — sphere radius for Circle / CircleInteract / CombatArena.
//                 total width of the active segment of a Plane trigger (metres).
//                 Only crossings within ±RadiusWidth/2 of the centre count.
//                 Default 10.0 m.
//   TriggerType — which geometry / game event activates this point.
//   PlaneAngle  — compass heading the plane faces, in degrees.
//                 Captured automatically by the config window; 0° = east (+X axis),
//                 90° = north (+Z axis). Edit manually with caution.
// ---------------------------------------------------------------------------
struct RoutePoint
{
    unsigned int    MapID               = 0;
    float           X                   = 0.0f;
    float           Y                   = 0.0f;
    float           Z                   = 0.0f;
    float           RadiusWidth         = 10.0f;
    ETriggerType    TriggerType         = ETriggerType::Circle;
    float           PlaneAngle          = 0.0f;
    int             HyperbolaC          = 12;   // MapChange only: controls the openness of the
                                                // hyperbolic corner cutout. C = HyperbolaC * 100.
                                                // Higher = wider curve. Default 12.
    int             DotDensity          = 200;  // Display only: >0 renders as a dot cloud with
                                                // this many dots (Circle) or derived spacing (Plane).
    float           bandCenterInput     = 0.0f; // Circle: centre latitude of the dot band in degrees
                                                // (-90 = south pole, +90 = north pole).
    float           bandUpInput         = 10.0f;// Extent upward from band centre (degrees for Circle,
                                                // metres for Plane). Dots fade to 0 at this boundary.
    float           bandDownInput       = 0.0f; // Extent downward from band centre (degrees for Circle,
                                                // metres for Plane). Dots fade to 0 at this boundary.
};

// ---------------------------------------------------------------------------
// CheckpointState
// ---------------------------------------------------------------------------
// A named waypoint in the route.  Holds both the persistent config (Point,
// Name, IsStart, IsGoal) and all per-run runtime state for that checkpoint.
//
// CONFIG — set on route load; survive across FullReset():
//   Point   — the spatial trigger definition.
//   Name    — display name shown in the timer and history.
//   IsStart — the timer starts (or resets + starts) when this point fires.
//             Only one checkpoint per route should have this set.
//   IsGoal  — the timer stops and the run is recorded when this point fires.
//             Multiple checkpoints may have this set, allowing routes that
//             diverge and finish at different endpoints.
//
// RUNTIME — zeroed by ResetRuntime() on every FullReset():
//   wasInCircle — true when the player was inside this checkpoint's sphere on
//                 the previous frame.  Used by Circle (start: fires on exit;
//                 non-start: fires on entry edge), CircleInteract, and
//                 CombatArena to track enter/leave edges.
//                 Always false for Plane, MapChange, AllCheckpoints.
//   triggered   — true once this checkpoint has fired at least once this run.
//                 Prevents a checkpoint from splitting twice in one run.
//                 For CombatArena, set in sync with combat.finished.
//   combat      — CombatArena state machine.  Ignored for all other types.
// ---------------------------------------------------------------------------
struct CheckpointState
{
    // ── Config (populated from route file; survives FullReset) ──────────────
    RoutePoint  Point;
    char        Name[64] = {};
    bool        IsStart  = false;
    bool        IsGoal   = false;

    // ── Runtime (zeroed by ResetRuntime on every FullReset) ─────────────────

    // Enter/exit edge tracking for Circle, CircleInteract, CombatArena.
    // "Was the player inside this zone on the previous frame?"
    bool               wasInCircle = false;

    // True once this checkpoint has fired at least once this run.
    // For CombatArena, set in sync with combat.finished.
    // Not used on IsStart checkpoints (start re-arms via its own path).
    bool               triggered   = false;

    // CombatArena state machine.  Ignored for all other trigger types.
    CombatTriggerState combat      = {};

    // Zeroes only the runtime fields, preserving config.
    // Called by FullReset() on every run reset.
    void ResetRuntime()
    {
        wasInCircle = false;
        triggered   = false;
        combat      = {};
    }
};

// ---------------------------------------------------------------------------
// Route
// ---------------------------------------------------------------------------
// The full route: an ordered list of CheckpointStates and an IsValid flag.
// IsValid must be set to true (via "Activate Route" in the config window)
// before AddonRender() will evaluate any triggers.  This prevents a partially
// edited route from accidentally starting the timer mid-edit.
// ---------------------------------------------------------------------------
struct Route
{
    std::vector<CheckpointState> Checkpoints;
    bool                         IsValid = false;
};

// ---------------------------------------------------------------------------
// GetStart / GetGoal  (inline helpers)
// ---------------------------------------------------------------------------
// Linear scans to find the designated start or goal checkpoint by flag.
// Returns the first goal checkpoint, or nullptr if none is set.
// For routes with multiple goals, iterate Checkpoints directly.
// ---------------------------------------------------------------------------
inline CheckpointState* GetStart(Route& route)
{
    for (auto& cp : route.Checkpoints)
        if (cp.IsStart) return &cp;
    return nullptr;
}

inline CheckpointState* GetGoal(Route& route)
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
