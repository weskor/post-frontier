#include "GroundHeight.h"

#include "Engine/World.h"

namespace
{
constexpr double ProbeTop = 3000.0;
constexpr double ProbeBottom = -1000.0;
constexpr double MinUpwardNormal = 0.3;
}

double GroundHeight::At(const UWorld& World, double X, double Y, double Fallback)
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundHeight), false);
	FHitResult Hit;
	if (!World.LineTraceSingleByObjectType(Hit, FVector(X, Y, ProbeTop), FVector(X, Y, ProbeBottom), Objects, Query)
		|| Hit.ImpactNormal.Z < MinUpwardNormal)
		return Fallback;
	return Hit.ImpactPoint.Z;
}

FVector GroundHeight::Snap(const UWorld& World, const FVector& Location)
{
	return FVector(Location.X, Location.Y, At(World, Location.X, Location.Y, Location.Z));
}
