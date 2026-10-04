#pragma once

#include "CoreMinimal.h"

class UWorld;

// Height of the walkable ground, for surfaces that must follow plateaus and ramps instead of assuming z = 0.
// Ground is the top-most up-facing static surface (plateau, ramp, floor); cover props and rocks are dynamic-object
// obstacles, so a probe over them still finds the ground. Works without a navmesh, so clients and the server agree.
namespace GroundHeight
{
// Ground z at (X, Y), or Fallback when nothing static lies below.
double At(const UWorld& World, double X, double Y, double Fallback = 0.0);

// Location with its z replaced by the ground height under it (the input z is the fallback).
FVector Snap(const UWorld& World, const FVector& Location);
}
