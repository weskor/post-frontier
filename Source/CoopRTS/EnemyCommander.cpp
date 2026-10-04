#include "EnemyCommander.h"

#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "EnemyCommanderTurn.h"
#include "Content/MatchContent.h"
#include "Headquarters.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

namespace
{
int32 FirstBuildingWith(const UMatchContent& Content, bool UBuildingDefinition::* Capability)
{
	for (int32 Index = 0; Index < Content.Buildings.Num(); ++Index)
		if (const UBuildingDefinition* Building = Content.Building(Index); Building && Building->*Capability)
			return Index;
	return INDEX_NONE;
}

// False when the match content lacks a producer or one unit of each combat role.
bool ResolveContent(FJevTurn& Turn)
{
	const UMatchContent& Content = *Turn.State->Content;
	Turn.Content = &Content;
	Turn.ProducerIndex = FirstBuildingWith(Content, &UBuildingDefinition::bProducesForces);
	Turn.ExtractorIndex = FirstBuildingWith(Content, &UBuildingDefinition::bRequiresDeposit);
	Turn.WorkshopIndex = FirstBuildingWith(Content, &UBuildingDefinition::bOffersResearch);
	const int32 FrontlineIndex = Content.UnitIndexForRole(EUnitRole::Frontline);
	const int32 RangedIndex = Content.UnitIndexForRole(EUnitRole::Ranged);
	const int32 SiegeIndex = Content.UnitIndexForRole(EUnitRole::Siege);
	if (Turn.ProducerIndex < 0 || FrontlineIndex < 0 || RangedIndex < 0 || SiegeIndex < 0)
		return false;
	Turn.Infantry = Content.Unit(FrontlineIndex);
	Turn.Reserve = Turn.Infantry->UnitCost * Turn.Infantry->Capacity;
	return true;
}
}

AEnemyCommander::AEnemyCommander()
{
	PrimaryActorTick.bCanEverTick = true;
	// Replicated only for the release schedule clients read; planning stays on the server.
	bReplicates = true;
	bAlwaysRelevant = true;
	SetNetUpdateFrequency(10.f);
	Release.NextAt = JevRelease::ReleaseTime(Release.Next);
}

void AEnemyCommander::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AEnemyCommander, Release);
	DOREPLIFETIME(AEnemyCommander, TeamIndex);
}

void AEnemyCommander::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority())
		return;
	// A due release evaluates at once, retrying while an evaluation cannot launch it.
	ReleaseRetryElapsed += DeltaSeconds;
	const bool bReleaseDue = TickRelease() && ReleaseRetryElapsed >= .25f;
	if ((EvaluateElapsed += DeltaSeconds) < 2.f && !bReleaseDue)
		return;
	ReleaseRetryElapsed = 0.f;
	EvaluateElapsed = FMath::Fmod(EvaluateElapsed, 2.f);
	EvaluatePlan();
}

bool AEnemyCommander::BeginTurn(FJevTurn& Turn)
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !State || !State->Content || State->MatchResult != EMatchResult::Ongoing
		|| (TeamIndex != 0 && TeamIndex != 5) || !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters))
		return false;
	if (!IsValid(Commander) && TeamIndex == 5)
		Commander = State->EnemyCommander;
	if (!IsValid(Commander) || Commander->TeamIndex != TeamIndex)
		return false;
	if (!bMemoLoadAttempted)
	{
		bMemoLoadAttempted = true;
		bMemosLoaded = MemoTemplates.Load();
	}
	if (!bMemosLoaded)
		return false;
	Turn.World = GetWorld();
	Turn.State = State;
	Turn.Commander = Commander;
	Turn.Team = TeamIndex;
	Turn.bRush = IsRushAutopilot();
	Turn.Now = GetWorld()->GetTimeSeconds();
	CommittedForces.RemoveAllSwap([&](const FJevCommittedForce& Entry) {
		return !Entry.Force.IsValid() || Entry.Force->GetOwningPlayerState() != Commander
			|| Entry.Force->GetAliveCount() == 0;
	});
	WaveForces.RemoveAllSwap([](const TWeakObjectPtr<AArmyGroup>& Force) { return !Force.IsValid() || Force->GetAliveCount() == 0; });
	if (TeamIndex == 5)
		State->EnemyPlans.RemoveAll([&](const FJevPublishedPlan& Entry) {
			return !IsValid(Entry.Force) || Entry.Force->GetOwningPlayerState() != Commander
				|| Entry.Force->GetAliveCount() == 0;
		});
	if (!ResolveContent(Turn))
		return false;
	Turn.Home = (TeamIndex == 5 ? State->EnemyHeadquarters : State->FriendlyHeadquarters)->GetActorLocation();
	Turn.EnemyHome = (TeamIndex == 5 ? State->FriendlyHeadquarters : State->EnemyHeadquarters)->GetActorLocation();
	return true;
}

void AEnemyCommander::EvaluatePlan()
{
	FJevTurn Turn;
	if (!BeginTurn(Turn) || !JevWorld::SummariseRegions(Turn))
		return;
	JevWorld::ScanBuildings(Turn);
	JevEconomy::PlaceFirstProducer(Turn);
	JevWorld::ScanForces(Turn);
	JevWorld::ScanUnits(Turn);
	JevWorld::ScanDeposits(Turn);
	JevEconomy::BuildExtractor(Turn);
	JevWorld::Finish(Turn);
	JevEconomy::ConfigureProduction(Turn);
	ExecuteForces(Turn);
	if (TeamIndex == 5)
		AdvanceReleases(Turn);
	JevEconomy::BuildNext(Turn);
	if (TeamIndex == 5)
		Turn.State->ForceNetUpdate();
}
