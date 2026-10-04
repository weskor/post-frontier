#pragma once

#include "CoreMinimal.h"

class UWorld;

// Height of the walkable ground, for surfaces that must follow plateaus and ramps instead of assuming z = 0.
// Ground is static collision whose actor carries the tag below (the V2 generator tags its floor, plateau, wall and ramp
// pieces); cover props, rocks, halls and buildings are never ground. A map with no tagged ground behaves as flat ground
// at the caller's fallback, so older maps keep their z = 0 rules. Works without a navmesh: clients and server agree.
namespace GroundHeight
{
inline const FName Tag = TEXT("Ground");

// Ground z at (X, Y), or Fallback when no tagged up-facing ground lies below.
double At(const UWorld& World, double X, double Y, double Fallback = 0.0);

// Location with its z replaced by the ground height under it (the input z is the fallback).
FVector Snap(const UWorld& World, const FVector& Location);

// First ground a view ray meets (plateau top, ramp deck, floor, or a cliff face); false when it meets none.
bool Ray(const UWorld& World, const FVector& Origin, const FVector& Direction, FVector& OutPoint);
}
