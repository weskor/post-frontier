#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderTests
{
EStepResult FScenarioBase::Recruit()
{
	if (!Check(Producer.IsValid(), TEXT("Paid producer survives initial recruitment")))
		return EStepResult::Finished;
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GameWorld);
	if (!Nav || Nav->IsNavigationBuildInProgress())
		return EStepResult::Waiting;
	if (!bConfigured)
	{
		if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true).IsAccepted(),
				TEXT("Paid producer configures frontline force through owner command")))
			return EStepResult::Finished;
		Force = Producer->ForceGroup;
		if (!Check(Force.IsValid() && Force->GetCapacity() == 6, TEXT("Real frontline definition supplies six slots")))
			return EStepResult::Finished;
		RecruitBalance = Wallet->Resources;
		bConfigured = true;
	}
	if (!Check(Force.IsValid(), TEXT("Paid force survives initial recruitment")))
		return EStepResult::Finished;
	if (Scenario == EScenario::Rally)
	{
		if (!Check(Force->Verb == EForceVerb::MoveHold && Force->TargetRegionIndex == Target,
				TEXT("New force inherits producer rally region as MoveHold target")))
			return EStepResult::Finished;
		bVisitedIntermediate |= Occupies(Intermediate);
	}
	if (Force->GetJoinedCount() != 6)
		return EStepResult::Waiting;
	if (!Check(FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false).IsAccepted()
				&& Wallet->Resources == RecruitBalance - 6 * Producer->GetProductionCost(),
			TEXT("Six naturally joined recruits pay exactly six real unit prices")))
		return EStepResult::Finished;
	bFilled = true;
	SetStage(0);
	return EStepResult::Continue;
}

bool FScenarioBase::Prepare()
{
	GameWorld = ArmyTestSetup::World();
	if (!GameWorld || GameWorld->GetTimeSeconds() < 3.f || !ArmyTestSetup::NavigationReady(GameWorld))
		return false;
	PC = ArmyTestSetup::Controller(GameWorld);
	State = GameWorld->GetGameState<ACommandGameState>();
	Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!PC || !Wallet || Wallet->CommanderIndex < 0 || !MapReady(State) || !State->Content)
		return false;
	if (IsolateWorld() || FindRoute() || PrepareEncounters())
		return true;
	return PrepareForce();
}

bool FScenarioBase::IsolateWorld()
{
	for (TActorIterator<AEnemyCommander> It(GameWorld); It; ++It)
		It->Destroy();
	for (TActorIterator<ACommandBuilding> It(GameWorld); It; ++It)
		if (It->TeamIndex == 5)
			It->Destroy();
	for (TActorIterator<AArmyGroup> It(GameWorld); It; ++It)
	{
		if (It->GetTeamIndex() == 0)
			return Fail(TEXT("Verb scenario requires a fresh world without friendly armies"));
		It->Destroy();
	}
	State->bVerificationIncomePaused = true;
	Wallet->Resources = 10000;
	for (const AMapRegion* Candidate : State->Regions)
	{
		if (Candidate->HomeTeam == 0)
			Home = Candidate->RegionIndex;
		if (Candidate->HomeTeam == 5)
			EnemyHome = Candidate->RegionIndex;
	}
	if (!Check(Home != INDEX_NONE && EnemyHome != INDEX_NONE, TEXT("Real map supplies both HQ regions")))
		return true;
	return false;
}

bool FScenarioBase::FindRoute()
{
	TMap<int32, int32> Distance, First;
	FindPaths(State, Home, Distance, First);
	TArray<int32> Indices;
	Distance.GetKeys(Indices);
	Indices.Sort();
	for (int32 Index : Indices)
	{
		AMapRegion* Candidate = Region(State, Index);
		AMapRegion* Via = Region(State, First[Index]);
		if (Distance[Index] == 2 && Candidate->HomeTeam < 0 && Via->HomeTeam < 0
			&& IsValid(Candidate->Anchor) && IsValid(Via->Anchor)
			&& State->GetRegionController(Index) == -1 && State->GetRegionController(Via->RegionIndex) == -1)
		{
			Target = Index;
			Intermediate = Via->RegionIndex;
			break;
		}
	}
	if (!Check(Target != INDEX_NONE, TEXT("Generated map supplies a neutral two-step route")))
		return true;
	return false;
}

bool FScenarioBase::PrepareEncounters()
{
	EnemyWallet = State->EnemyCommander;
	if (!Check(EnemyWallet.IsValid(), TEXT("Authoritative enemy wallet survives planner isolation")))
		return true;
	Hostile = SpawnGroup(GameWorld, nullptr, -1, FromEnemyHQ(State, -800.f, 500.f, 100.f));
	if (!Check(Hostile.IsValid() && Hostile->GetAliveCount() == 6,
			TEXT("Stationary opposing encounter uses real registered combat members")))
		return true;
	Hostile->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Hostile->GetUnits())
	{
		Unit->SetActorTickEnabled(false);
		Unit->NextAttackTime = TNumericLimits<float>::Max();
	}
	ParkHostile();
	bPaid = Scenario == EScenario::Withdrawal || Scenario == EScenario::Retreat
		|| Scenario == EScenario::Orphan || Scenario == EScenario::Rally
		|| Scenario == EScenario::NoSafeRegion || Scenario == EScenario::StructureWithdrawal;
	bPaid |= Scenario == EScenario::ContestedTransit;
	return false;
}

bool FScenarioBase::PrepareForce()
{
	if (bPaid)
	{
		Producer = Place();
		if (!Producer.IsValid())
			return true;
		if (!Check(Producer->RallyRegionIndex == Home, TEXT("New producer defaults rally to its own real region")))
			return true;
		if (Scenario == EScenario::Rally && !Check(FCommandService::SetRallyPoint(Wallet, Producer.Get(), Target).IsAccepted() && Producer->RallyRegionIndex == Target, TEXT("Owner sets a rally before force configuration")))
			return true;
	}
	else
	{
		Force = SpawnGroup(GameWorld, PC, 0, FromFriendlyHQ(State, 800.f, 500.f, 100.f));
		if (!Check(Force.IsValid() && Force->GetAliveCount() == 6 && Force->GetJoinedCount() == 6,
				TEXT("Explicit mixed-role encounter starts with six joined members")))
			return true;
	}
	bPrepared = true;
	return false;
}

}

#endif
