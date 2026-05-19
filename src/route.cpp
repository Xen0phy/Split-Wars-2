// route.cpp
#include "route.h"
#include <cmath>

float DistanceTo(const Vector3& playerPos, const RoutePoint& point)
{
    float dx = playerPos.X - point.X;
    float dy = playerPos.Y - point.Y;
    float dz = playerPos.Z - point.Z;
    return std::sqrt(dx*dx + dy*dy + dz*dz);
}

bool IsWithinRange(const Vector3& playerPos, const RoutePoint& point)
{
    return DistanceTo(playerPos, point) <= point.Radius;
}

bool HasCrossedPlane(const Vector3& prevPos, const Vector3& currPos, const RoutePoint& point)
{
    float angleRad = point.PlaneAngle * 3.14159265f / 180.0f;
    float nx = std::cos(angleRad);
    float nz = std::sin(angleRad);

    float px = -nz;
    float pz =  nx;

    float dx = currPos.X - point.X;
    float dz = currPos.Z - point.Z;
    float alongPlane = dx * px + dz * pz;
    if (std::abs(alongPlane) > point.PlaneWidth * 0.5f) return false;

    float prevDx  = prevPos.X - point.X;
    float prevDz  = prevPos.Z - point.Z;
    float prevDot = prevDx * nx + prevDz * nz;
    float currDot = dx     * nx + dz     * nz;

    return (prevDot < 0.0f) != (currDot < 0.0f);
}