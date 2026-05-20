// route.h
#pragma once

#include "Mumble.h"
#include <vector>

enum class ETriggerType : unsigned char
{
    Circle        = 0,
    Plane         = 1,
    MapChange     = 2,
    AllCheckpoints = 3  // Goal only: fires once every checkpoint has been triggered
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