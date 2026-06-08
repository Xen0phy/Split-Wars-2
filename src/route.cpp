// route.cpp
// Implements the spatial trigger geometry used to detect whether a player
// has entered a zone or crossed a trigger plane.
//
// GW2 uses a right-handed Y-up coordinate system.  All position values come
// from MumbleLink and are in metres.  The Y axis is vertical, so horizontal
// distance checks operate on the X/Z plane.

#include "route.h"
#include <cmath>

// ---------------------------------------------------------------------------
// DistanceTo
// ---------------------------------------------------------------------------
// Returns the straight-line 3D distance between the player and a route point.
// Used internally by IsWithinRange and exposed for any caller that needs the
// raw distance (e.g. future "nearest checkpoint" UI features).
// ---------------------------------------------------------------------------
float DistanceTo(const Vector3& playerPos, const RoutePoint& point)
{
    float dx = playerPos.X - point.X;
    float dy = playerPos.Y - point.Y;
    float dz = playerPos.Z - point.Z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

// ---------------------------------------------------------------------------
// IsWithinRange
// ---------------------------------------------------------------------------
// Returns true when the player is inside the point's sphere of influence
// (3D distance <= Radius).  Used by Circle and CircleInteract trigger types.
// ---------------------------------------------------------------------------
bool IsWithinRange(const Vector3& playerPos, const RoutePoint& point)
{
    return DistanceTo(playerPos, point) <= point.RadiusWidth;
}

// ---------------------------------------------------------------------------
// HasCrossedPlane
// ---------------------------------------------------------------------------
// Returns true when the player's movement between prevPos and currPos crossed
// a finite trigger plane defined by the RoutePoint.
//
// The plane is described by three values stored on the point:
//   X, Z        — the centre of the plane in world space (Y is ignored)
//   PlaneAngle  — the compass heading the plane faces, in degrees.
//                 0° = north (+Z), 90° = east (+X), matching GW2 convention.
//   RadiusWidth — the total width of the active segment of the plane.
//                 Only crossings within ±RadiusWidth/2 of the centre fire.
//
// How it works:
//   1. Convert PlaneAngle to a normal vector (nx, nz) perpendicular to the
//      plane's surface.  This is the direction the plane "faces".
//   2. Derive the along-plane direction (px, pz) by rotating the normal 90°.
//   3. Check that the player's current position is within the half-width of
//      the plane along its surface direction (the width gate).
//   4. Compute the signed dot product of (prevPos - centre) and (currPos - centre)
//      against the normal.  If the signs differ the player crossed the plane
//      this frame, regardless of direction (triggers in both directions).
// ---------------------------------------------------------------------------
bool HasCrossedPlane(const Vector3& prevPos, const Vector3& currPos, const RoutePoint& point)
{
    // Step 1 — build the plane's normal vector from the configured angle.
    float angleRad = point.PlaneAngle * 3.14159265f / 180.0f;
    float nx = std::cos(angleRad); // Normal X component
    float nz = std::sin(angleRad); // Normal Z component

    // Step 2 — the along-plane (tangent) direction is the normal rotated 90°.
    float px = -nz;
    float pz =  nx;

    // Step 3 — width gate: project the player's offset onto the tangent and
    // reject if they are outside the active half-width of the plane.
    float dx = currPos.X - point.X;
    float dz = currPos.Z - point.Z;
    float alongPlane = dx * px + dz * pz;
    if (std::abs(alongPlane) > point.RadiusWidth * 0.5f) return false;

    // Step 3b — height gate: reject if player is outside the band above/below Y.
    float dy = currPos.Y - point.Y;
    if (dy >  point.bandUpInput)   return false;
    if (dy < -point.bandDownInput) return false;

    // Step 4 — sign-change test: project both positions onto the normal.
    // A sign change means the player moved from one side of the plane to the other.
    float prevDx  = prevPos.X - point.X;
    float prevDz  = prevPos.Z - point.Z;
    float prevDot = prevDx * nx + prevDz * nz;
    float currDot = dx     * nx + dz     * nz;

    return (prevDot < 0.0f) != (currDot < 0.0f);
}