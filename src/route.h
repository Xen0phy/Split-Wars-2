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
    CircleInteract  = 3,
    CombatArena     = 4,
    AllCheckpoints  = 5
};

enum class ECombatState : unsigned char
{
    Armed,
    GracePending,
    Finished
};

struct CombatTriggerState
{
    bool                                        active       = false;
    ECombatState                                state        = ECombatState::Armed;
    double                                      dropTime     = 0.0;
    std::chrono::steady_clock::time_point       graceStart;
    bool                                        finished     = false;
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
    bool        IsStart  = false;
    bool        IsGoal   = false;
};

struct Route
{
    std::vector<Checkpoint> Checkpoints;
    bool                    IsValid = false;
};

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

float   DistanceTo(const Vector3& playerPos, const RoutePoint& point);
bool    IsWithinRange(const Vector3& playerPos, const RoutePoint& point);
bool    HasCrossedPlane(const Vector3& prevPos, const Vector3& currPos, const RoutePoint& point);
