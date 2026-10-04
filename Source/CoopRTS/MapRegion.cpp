#include "MapRegion.h"

#include "CapturePoint.h"
#include "CommandGameState.h"
#include "Commands/AbilityCommandComponent.h"
#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"
#include "Rules/PlacementPolicy.h"

AMapRegion::AMapRegion()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	PrimaryActorTick.bCanEverTick = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void AMapRegion::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || FortifyTeam < 0)
		return;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return;
	const FortifyPolicy::EEnd End = FortifyPolicy::Review(GetFortify(), State->GetRegionController(RegionIndex), GetServerNow());
	if (End != FortifyPolicy::EEnd::None)
		EndFortify(End);
}

float AMapRegion::GetServerNow() const
{
	const AGameStateBase* State = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	return State ? State->GetServerWorldTimeSeconds() : 0.f;
}

bool AMapRegion::IsFortifyActive() const
{
	return FortifyPolicy::IsActive(GetFortify(), GetServerNow());
}

float AMapRegion::FortifyIncomingMultiplier(int32 VictimTeam) const
{
	return FortifyPolicy::IncomingFor(GetFortify(), VictimTeam, GetServerNow());
}

bool AMapRegion::IsCaptureFrozen() const
{
	return FortifyPolicy::FreezesCapture(GetFortify(), GetServerNow());
}

float AMapRegion::FortifyIncomingAt(const ACommandGameState& State, const FVector& Location, int32 VictimTeam)
{
	const AMapRegion* Region = State.FindRegionAt(Location);
	return Region ? Region->FortifyIncomingMultiplier(VictimTeam) : 1.f;
}

void AMapRegion::StartFortify(int32 Team, int32 CasterCommander)
{
	if (!HasAuthority())
		return;
	const FortifyPolicy::FRegionState Cast = FortifyPolicy::Cast(Team, GetServerNow());
	FortifyTeam = Cast.Team;
	FortifyExpiresAt = Cast.ExpiresAt;
	FortifyCaster = CasterCommander;
	ForceNetUpdate();
}

void AMapRegion::EndFortify(FortifyPolicy::EEnd Reason)
{
	if (!HasAuthority() || FortifyTeam < 0)
		return;
	const int32 Team = FortifyTeam;
	FortifyTeam = -1;
	FortifyCaster = -1;
	FortifyExpiresAt = 0.f;
	ForceNetUpdate();
	if (Reason == FortifyPolicy::EEnd::RegionLost)
		UAbilityCommandComponent::PostFortifyEnded(*this, Team);
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
	DOREPLIFETIME(AMapRegion, Trait);
	DOREPLIFETIME(AMapRegion, FortifyTeam);
	DOREPLIFETIME(AMapRegion, FortifyCaster);
	DOREPLIFETIME(AMapRegion, FortifyExpiresAt);
}
