#include "CommandGameState.h"

#include "ArmyGroup.h"
#include "CommandGameMode.h"
#include "CommandPlayerController.h"
#include "GameState/GameStatePlacement.h"
#include "GameState/GameStateRegistry.h"
#include "GameState/GameStateTerritory.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformTime.h"
#include "MatchTelemetry.h"
#include "Net/UnrealNetwork.h"
#include "ObjectiveAnnouncer.h"

ACommandGameState::ACommandGameState()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	ObjectiveAnnouncer = CreateDefaultSubobject<UObjectiveAnnouncer>(TEXT("Objective Announcer"));
	MatchTelemetry = CreateDefaultSubobject<UMatchTelemetry>(TEXT("Match Telemetry"));
}

void ACommandGameState::BeginPlay()
{
	Super::BeginPlay();
	AudioLiveStartServerTime = GetServerWorldTimeSeconds();
	LastAudioMatchResult = MatchResult;
	bMatchAudioInitialized = true;
	bOutcomeAudioPlayed = MatchResult != EMatchResult::Ongoing;
}

bool ACommandGameState::ApplyPause(ACommandPlayerController* Controller, bool bPause)
{
	// Nothing runs during planning, so there is nothing to pause and the shared pause is not spent.
	if (Planning.bActive && bPause)
		return false;
	const bool bCoop = GetNetMode() != NM_Standalone;
	if (bPause)
	{
		ACommandGameMode* Mode = GetWorld()->GetAuthGameMode<ACommandGameMode>();
		if (!PauseBudget.CanPause(bCoop) || !Mode || !Mode->ApplyMatchPause(Controller, true))
			return false;
		PauseBudget.Begin(bCoop, FPlatformTime::Seconds());
	}
	else
	{
		if (!PauseBudget.bPaused)
			return false;
		PauseBudget.Resume();
		if (!bSoloMenuPaused)
			GetWorld()->GetAuthGameMode<ACommandGameMode>()->ApplyMatchPause(Controller, false);
	}
	PublishPauseBudget();
	// Pausing freezes normal replication scheduling; publish the engine pause flag now.
	GetWorld()->GetWorldSettings()->ForceNetUpdate();
	return true;
}

void ACommandGameState::PublishPauseBudget()
{
	bActivePaused = PauseBudget.bPaused;
	bCoopPauseSpent = PauseBudget.bSpent;
	PauseSecondsRemaining = static_cast<float>(PauseBudget.Remaining(FPlatformTime::Seconds()));
	ForceNetUpdate();
}

void ACommandGameState::RefreshSoloMenuPause(ACommandPlayerController* Controller, bool bMenuPaused)
{
	if (!HasAuthority() || GetNetMode() != NM_Standalone)
		return;
	bSoloMenuPaused = bMenuPaused;
	SyncWorldPause(Controller);
}

void ACommandGameState::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (HasAuthority() && Planning.bActive)
	{
		TickPlanning(); // Real-time bookkeeping while the world stands still, never income.
		return;
	}
	if (HasAuthority() && PauseBudget.bPaused)
	{
		const double Now = FPlatformTime::Seconds();
		if (PauseBudget.Expired(Now))
		{
			PauseBudget.Resume();
			GetWorld()->GetAuthGameMode<ACommandGameMode>()->ApplyMatchPause(nullptr, false);
			GetWorld()->GetWorldSettings()->ForceNetUpdate();
		}
		PublishPauseBudget();
		return; // This tick is real-time bookkeeping, never income.
	}
	if (GetWorld()->IsPaused())
		return;
	if (!HasAuthority() || MatchResult != EMatchResult::Ongoing)
		return;
	if (EnemyPlans.RemoveAll([](const FJevPublishedPlan& Plan) {
			return !IsValid(Plan.Force) || Plan.Force->GetAliveCount() == 0;
		}))
		ForceNetUpdate();
	if ((HoldAlarmElapsed += DeltaSeconds) >= .25f)
	{
		HoldAlarmElapsed = 0.f;
		UpdateRegionAlarms();
	}
	GameStateTerritory::RefreshConnections(*this);
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	if (bVerificationIncomePaused)
		return;
#endif
	Economy.Tick(*this, DeltaSeconds);
}

int32 ACommandGameState::GetHumanBaselineIncomePerSecond() const
{
	return FGameStateEconomy::HumanBaselineIncomePerSecond(GetWorld());
}

int32 ACommandGameState::GetJevBaselineIncomePerSecond() const
{
	return FGameStateEconomy::JevBaselineIncomePerSecond(GetWorld());
}

double ACommandGameState::GetEnemyBaselineIncomePerSecond() const
{
	return FGameStateEconomy::EnemyBaselineIncomePerSecond(*this);
}

int32 ACommandGameState::GetIncomePerSecond(const ACommandPlayerState* Commander) const
{
	return FGameStateEconomy::IncomePerSecond(*this, Commander);
}

int32 ACommandGameState::GetEnemyIncomePerSecond() const
{
	return GetIncomePerSecond(EnemyCommander);
}

double ACommandGameState::GetPowerRate(const ACommandPlayerState* Commander) const
{
	return FGameStateEconomy::PowerRate(*this, Commander);
}

double ACommandGameState::GetDataRate(const ACommandPlayerState* Commander) const
{
	return FGameStateEconomy::DataRate(*this, Commander);
}

uint64 ACommandGameState::GetConnectedMask(int32 Team) const
{
	return Team == 0 ? HumanConnection.Mask : Team == 5 ? EnemyConnection.Mask
														: 0;
}

float ACommandGameState::GetConnectionChangedAt(int32 Team) const
{
	return Team == 0 ? HumanConnection.ChangedAt : Team == 5 ? EnemyConnection.ChangedAt
															 : 0.f;
}

bool ACommandGameState::IsRegionConnected(int32 Team, int32 RegionIndex) const
{
	return RegionIndex >= 0 && RegionIndex < ForceOrders::MaxRegions
		&& (GetConnectedMask(Team) & (uint64(1) << RegionIndex)) != 0;
}

const AMapRegion* ACommandGameState::FindRegionAt(const FVector& Location) const
{
	return GameStateRegistry::FindRegionAt(*this, Location);
}

int32 ACommandGameState::GetRegionController(int32 RegionIndex) const
{
	return GameStateTerritory::RegionController(*this, RegionIndex);
}

bool ACommandGameState::IsRegionContested(int32 RegionIndex, int32 ForTeam) const
{
	return GameStateTerritory::IsRegionContested(*this, RegionIndex, ForTeam);
}

FVector ACommandGameState::GetRegionAnchor(int32 RegionIndex) const
{
	return GameStateTerritory::RegionAnchor(*this, RegionIndex);
}

FVector ACommandGameState::ResolveBuildingLocation(int32 BuildingIndex, const FVector& RequestedLocation, int32 Team) const
{
	return GameStatePlacement::ResolveLocation(*this, BuildingIndex, RequestedLocation, Team);
}

bool ACommandGameState::IsInBuildTerritory(int32 BuildingIndex, int32 Team, const FVector& RequestedLocation) const
{
	return GameStatePlacement::IsInBuildTerritory(*this, BuildingIndex, Team, RequestedLocation);
}

bool ACommandGameState::ValidateBuildingPlacement(int32 BuildingIndex, int32 Team, const FVector& RequestedLocation, FString& OutReason) const
{
	return GameStatePlacement::Validate(*this, BuildingIndex, Team, RequestedLocation, OutReason);
}

void ACommandGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACommandGameState, Content);
	DOREPLIFETIME(ACommandGameState, Regions);
	DOREPLIFETIME(ACommandGameState, Deposits);
	DOREPLIFETIME(ACommandGameState, CaptureSites);
	DOREPLIFETIME(ACommandGameState, Buildings);
	DOREPLIFETIME(ACommandGameState, MatchResult);
	DOREPLIFETIME(ACommandGameState, FriendlyHeadquarters);
	DOREPLIFETIME(ACommandGameState, EnemyHeadquarters);
	DOREPLIFETIME(ACommandGameState, Arena);
	DOREPLIFETIME(ACommandGameState, HumanConnection);
	DOREPLIFETIME(ACommandGameState, EnemyConnection);
	DOREPLIFETIME(ACommandGameState, GiftLog);
	DOREPLIFETIME(ACommandGameState, EnemyPlans);
	DOREPLIFETIME(ACommandGameState, EnemyCommander);
	DOREPLIFETIME(ACommandGameState, bActivePaused);
	DOREPLIFETIME(ACommandGameState, bCoopPauseSpent);
	DOREPLIFETIME(ACommandGameState, PauseSecondsRemaining);
	DOREPLIFETIME(ACommandGameState, Planning);
	DOREPLIFETIME(ACommandGameState, BattleClockStartServerTime);
}
