// route.h
#pragma once

#include "Mumble.h"
#include <vector>
#include <chrono>

enum class ETriggerType : unsigned char
{
    Circle          = 0,
    Plane           = 1,
    MapChange       = 2,
    CircleInteract  = 3,    // Fires when interact key is pressed inside the circle
    CombatArena     = 4,     // Arms on combat-enter inside circle, fires finish on combat-leave or circle-leave
    AllCheckpoints  = 5   // Goal only: fires once every checkpoint has been triggered
};

enum class ECombatState : unsigned char
{
    Armed,          // In combat inside the circle
    GracePending,   // Combat dropped, waiting grace period before finishing
    Finished        // Trigger has fired its finish event; cannot re-arm
};

struct CombatTriggerState
{
    bool                                        active       = false;  // true = Armed or GracePending
    ECombatState                                state        = ECombatState::Armed;
    double                                      dropTime     = 0.0;    // timer value when combat dropped
    std::chrono::steady_clock::time_point       graceStart;            // wall clock when grace began
    bool                                        finished     = false;  // has fired finish; locked out
};

struct RoutePoint
{
    unsigned int    MapID        = 0;
    float           X            = 0.0f;
    float           Y            = 0.0f;
    float           Z            = 0.0f;
    float           Radius       = 5.0f;
    ETriggerType    TriggerType  = ETriggerType::Circle;
    float           PlaneWidth   = 10.0f;
    float           PlaneAngle   = 0.0f;
};

struct Checkpoint
{
    RoutePoint  Point;
    char        Name[64] = {};
};

struct Route
{
    RoutePoint              Start;
    RoutePoint              Goal;
    std::vector<Checkpoint> Checkpoints;
    bool                    IsValid = false;
};

float   DistanceTo(const Vector3& playerPos, const RoutePoint& point);
bool    IsWithinRange(const Vector3& playerPos, const RoutePoint& point);
bool    HasCrossedPlane(const Vector3& prevPos, const Vector3& currPos, const RoutePoint& point);