#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "FormationFitSite.h"

#include "CapturePoint.h"
#include "MapRegion.h"
#include "NavigationSystem.h"
#include "Rules/HoldPolicy.h"

namespace
{
constexpr double MinimumEdgeLength = 2500.;
// Open ground a force needs around its centre: beside the border and inward.
constexpr double SideRoom = 300., InwardRoom = 400.;

bool Navigable(UNavigationSystemV1& Navigation, const FVector2D& Point, double Z, FVector& Ground)
{
	FNavLocation Projected;
	if (!Navigation.ProjectPointToNavigation(FVector(Point.X, Point.Y, Z), Projected, FVector(35., 35., 400.))
		|| FVector2D::Distance(Point, FVector2D(Projected.Location)) > 20.)
		return false;
	Ground = Projected.Location;
	return true;
}

// A point Inset inside the edge at Along, with the border's other edges well away and ground to spare.
bool EdgeSpot(UNavigationSystemV1& Navigation, const AMapRegion& Region, const FVector2D& From, const FVector2D& To,
	double Along, double Inset, double Z, FVector& Ground)
{
	const FVector2D Direction = (To - From).GetSafeNormal();
	FVector2D Normal(-Direction.Y, Direction.X);
	const FVector2D OnEdge = From + (To - From) * Along;
	if (!HoldPolicy::Contains(Region.Polygon, OnEdge + Normal))
		Normal = -Normal;
	const FVector2D Spot = OnEdge + Normal * Inset;
	if (!HoldPolicy::Contains(Region.Polygon, Spot)
		|| FMath::Abs(FVector2D::Distance(Spot, HoldPolicy::ClosestBoundary(Region.Polygon, Spot)) - Inset) > 1.)
		return false;
	FVector Unused;
	for (const FVector2D& Offset : { Direction * -SideRoom, Direction * SideRoom, Normal * InwardRoom })
		if (!HoldPolicy::Contains(Region.Polygon, Spot + Offset) || !Navigable(Navigation, Spot + Offset, Z, Unused))
			return false;
	return Navigable(Navigation, Spot, Z, Ground);
}
}

double FormationFitSite::Clearance(const AMapRegion& Region, const FVector& Point)
{
	const FVector2D Flat(Point);
	const double Distance = FVector2D::Distance(Flat, HoldPolicy::ClosestBoundary(Region.Polygon, Flat));
	return HoldPolicy::Contains(Region.Polygon, Flat) ? Distance : -Distance;
}

bool FormationFitSite::Find(UNavigationSystemV1& Navigation, const AMapRegion& Region, double AnchorInset,
	double PostInset, FSite& Site)
{
	const TArray<FVector2D>& Polygon = Region.Polygon;
	const double Z = Region.GetActorLocation().Z;
	for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
	{
		const FVector2D& From = Polygon[Previous];
		const FVector2D& To = Polygon[Index];
		if (FVector2D::Distance(From, To) < MinimumEdgeLength)
			continue;
		FVector Anchor, Post;
		if (EdgeSpot(Navigation, Region, From, To, .3, AnchorInset, Z, Anchor)
			&& EdgeSpot(Navigation, Region, From, To, .7, PostInset, Z, Post))
		{
			Site.Anchor = Anchor;
			Site.Post = Post;
			return true;
		}
	}
	return false;
}

void FormationFitSite::Apply(AMapRegion& Region, const FSite& Site)
{
	if (IsValid(Region.Anchor))
		Region.Anchor->SetActorLocation(Site.Anchor);
	Region.DefendPosts = { Site.Post };
}
#endif
