#include "MapRegion.h"

#include "CapturePoint.h"
#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"
#include "Rules/PlacementPolicy.h"

AMapRegion::AMapRegion()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

bool AMapRegion::Contains(const FVector& WorldLocation) const
{
	return PlacementPolicy::ContainsPoint(Polygon, FVector2D(WorldLocation.X, WorldLocation.Y));
}

void AMapRegion::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AMapRegion, RegionIndex);
	DOREPLIFETIME(AMapRegion, DisplayName);
	DOREPLIFETIME(AMapRegion, RegionRole);
	DOREPLIFETIME(AMapRegion, HomeTeam);
	DOREPLIFETIME(AMapRegion, Polygon);
	DOREPLIFETIME(AMapRegion, DefendPosts);
	DOREPLIFETIME(AMapRegion, Neighbours);
	DOREPLIFETIME(AMapRegion, Anchor);
}
