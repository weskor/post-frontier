#pragma once

#include "CoreMinimal.h"

// The one distance rule for weapon range. A structure is a box (its footprint or hit box, turned by
// the actor's yaw) and range runs from the attacker's capsule to its edge. A unit is a point: range
// stays centre to centre, as the duel matrix was balanced. Everything is planar. Firing, pursuit and
// acquisition all read EdgeDistance.
namespace CombatRangePolicy
{
// What is measured to. Clearance, the attacker's own radius against a structure, is subtracted from
// the raw distance.
struct FRangeTarget
{
	FRangeTarget() = default;
	// A bare point: a unit, or any target measured centre to centre.
	FRangeTarget(const FVector& Point) : Center(Point) {}
	FVector2D Center = FVector2D::ZeroVector;
	// Half sizes of the box along its own axes; zero for a point.
	FVector2D HalfExtent = FVector2D::ZeroVector;
	float YawDegrees = 0.f;
	float Clearance = 0.f;
	// A structure whose footprint cuts the navigation mesh: units cannot stand within it or close to
	// its origin, so approaching it needs the edge rules of Rules/PursuitPolicy.h.
	bool bBlocksMovement = false;
};

// A structure: box of half sizes HalfExtent turned by YawDegrees.
FRangeTarget Box(const FVector& Center, const FVector2D& HalfExtent, float YawDegrees, float AttackerRadius, bool bBlocksMovement = true);

// Whether the target is a structure (a box that stands still) rather than a unit (a point).
bool IsStructure(const FRangeTarget& Target);

// Closest point of the target's body to From (the centre of a disc; From itself inside a box).
FVector2D NearestPoint(const FVector2D& From, const FRangeTarget& Target);
// Planar distance from From to the target's edge minus its clearance; never negative.
double EdgeDistance(const FVector2D& From, const FRangeTarget& Target);
// Whether a weapon of this range reaches the target.
bool InRange(const FVector2D& From, const FRangeTarget& Target, double Range);

// A point at EdgeDistance Standoff from the target, on the ray from the nearest point of its body
// through From. From inside a box leaves through the nearest face; at a point target the way out of
// its own centre is toward -X.
FVector2D PointAtEdgeDistance(const FVector2D& From, const FRangeTarget& Target, double Standoff);
}
