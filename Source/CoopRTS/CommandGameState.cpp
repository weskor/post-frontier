#include "CommandGameState.h"

#include "CapturePoint.h"
#include "CommandPlayerState.h"
#include "Net/UnrealNetwork.h"

ACommandGameState::ACommandGameState()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
}

void ACommandGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || MatchResult != EMatchResult::Ongoing) return;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	if (bVerificationIncomePaused) return;
#endif
	IncomeElapsed += DeltaSeconds;
	while (IncomeElapsed >= 2.f)
	{
		IncomeElapsed -= 2.f;
		for (APlayerState* Player : PlayerArray)
		{
			if (ACommandPlayerState* Wallet = Cast<ACommandPlayerState>(Player))
			{
				Wallet->AddResources(GetIncomePerSecond() * 2);
			}
		}
		EnemyResources += GetEnemyIncomePerSecond() * 2;
		ForceNetUpdate();
	}
}

int32 ACommandGameState::GetEnemyIncomePerSecond() const
{
	int32 Sites = 0;
	for (const ACapturePoint* Site : CaptureSites)
		if (IsValid(Site) && Site->SiteKind == ECaptureSiteKind::Resource && Site->ControllingTeam == 5) ++Sites;
	return BaselineIncomePerSecond + ResourceIncomePerSecond * Sites;
}

void ACommandGameState::RefreshTerritory()
{
	if (!HasAuthority()) return;
	int32 Sites = 0;
	int32 Forward = -1;
	for (const ACapturePoint* Site : CaptureSites)
	{
		if (!IsValid(Site)) continue;
		if (Site->SiteKind == ECaptureSiteKind::Resource && Site->ControllingTeam == 0) ++Sites;
		if (Site->SiteKind == ECaptureSiteKind::Reinforcement) Forward = Site->ControllingTeam;
	}
	if (Sites != ControlledResourceSites || Forward != ForwardSiteTeam)
	{
		ControlledResourceSites = Sites;
		ForwardSiteTeam = Forward;
		ForceNetUpdate();
	}
}

void ACommandGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACommandGameState, ControlledResourceSites);
	DOREPLIFETIME(ACommandGameState, ForwardSiteTeam);
	DOREPLIFETIME(ACommandGameState, CaptureSites);
	DOREPLIFETIME(ACommandGameState, MatchResult);
	DOREPLIFETIME(ACommandGameState, FriendlyHeadquarters);
	DOREPLIFETIME(ACommandGameState, EnemyHeadquarters);
	DOREPLIFETIME(ACommandGameState, EnemyPlan);
	DOREPLIFETIME(ACommandGameState, EnemyPlanRationale);
	DOREPLIFETIME(ACommandGameState, EnemyResources);
}
