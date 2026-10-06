#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"

class AMapRegion;
class UNavigationSystemV1;

// Test fixture for forces that stand close to a region's border: finds open, navigable ground a set distance
// inside one polygon edge and moves the region's anchor and defend post there, so a scenario exercises the
// formation fit on the real map instead of a hand-made one.
namespace FormationFitSite
{
struct FSite
{
	FVector Anchor = FVector::ZeroVector;
	FVector Post = FVector::ZeroVector;
};

// Two points along one long edge of Region, AnchorInset and PostInset centimetres inside it, with nothing
// else of the border nearby and navigable ground around both. False when no edge offers that.
bool Find(UNavigationSystemV1& Navigation, const AMapRegion& Region, double AnchorInset, double PostInset, FSite& Site);
// The region's capture anchor moves to Site.Anchor and its only defend post becomes Site.Post.
void Apply(AMapRegion& Region, const FSite& Site);
// Distance from Point to the nearest polygon edge, negative outside the polygon.
double Clearance(const AMapRegion& Region, const FVector& Point);
}
#endif
