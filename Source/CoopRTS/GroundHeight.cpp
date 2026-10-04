#include "GroundHeight.h"

#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace
{
constexpr double ProbeTop = 3000.0;
constexpr double ProbeBottom = -1000.0;
constexpr double RayReach = 200000.0;
constexpr double MinUpwardNormal = 0.3;

bool IsGround(const FHitResult& Hit)
{
	const AActor* Actor = Hit.GetActor();
	return Actor && Actor->ActorHasTag(GroundHeight::Tag);
}

bool FirstGround(const UWorld& World, const FVector& Start, const FVector& End, bool bUpFacingOnly, FHitResult& OutHit)
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(ECC_WorldStatic);
	FCollisionQueryParams Query(SCENE_QUERY_STAT(GroundHeight), false);
	TArray<FHitResult> Hits;
	World.LineTraceMultiByObjectType(Hits, Start, End, Objects, Query);
	for (const FHitResult& Hit : Hits)
		if (IsGround(Hit) && (!bUpFacingOnly || Hit.ImpactNormal.Z >= MinUpwardNormal))
		{
			OutHit = Hit;
			return true;
		}
	return false;
}
}

double GroundHeight::At(const UWorld& World, double X, double Y, double Fallback)
{
	FHitResult Hit;
	return FirstGround(World, FVector(X, Y, ProbeTop), FVector(X, Y, ProbeBottom), true, Hit) ? Hit.ImpactPoint.Z : Fallback;
}

FVector GroundHeight::Snap(const UWorld& World, const FVector& Location)
{
	return FVector(Location.X, Location.Y, At(World, Location.X, Location.Y, Location.Z));
}

bool GroundHeight::Ray(const UWorld& World, const FVector& Origin, const FVector& Direction, FVector& OutPoint)
{
	FHitResult Hit;
	if (!FirstGround(World, Origin, Origin + Direction.GetSafeNormal() * RayReach, false, Hit))
		return false;
	OutPoint = Hit.ImpactPoint;
	return true;
}
