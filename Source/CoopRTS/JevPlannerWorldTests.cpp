#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevPlannerWorldFixture.h"
#include "CapturePoint.h"

bool FJevPlannerWorldScenario::Update()
{
	if (FPlatformTime::Seconds() - Started > 100.)
		return Fail(*FString::Printf(TEXT("JEV world proof %d exceeded bounded deadline at stage %d"), int32(Proof), Stage));
	UWorld* World = ArmyTestSetup::World();
	ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
	if (!ArmyTestSetup::MapReady(State) || !PC || ArmyTestSetup::GameSeconds(World) < 3.
		|| (Stage == 0 && !ArmyTestSetup::NavigationReady(World)))
		return false;
	if (State->MatchResult != EMatchResult::Ongoing)
		return Fail(TEXT("Isolated planner fixture must not end the match"));
	if (Stage == 0)
		return Begin(World, State, PC);
	if (!Planner.IsValid() || !Forces[0].IsValid() || !Forces[1].IsValid())
		return Fail(TEXT("Both independent producer forces and their tested commander must survive"));
	const float Now = ArmyTestSetup::GameSeconds(World);
	if (Stage == 1)
		return Stage1(State, Now);
	return Progress(World, State, PC, Now);
}

bool FJevPlannerWorldScenario::Stage1(ACommandGameState* State, float Now)
{
	for (const TWeakObjectPtr<AArmyGroup>& Force : Forces)
		if (Force->GetJoinedCount() != 6)
			return false;
	for (const TWeakObjectPtr<AArmyGroup>& Force : Forces)
	{
		ACommandBuilding* Producer = Force->GetProductionBuilding();
		FCommandService::ConfigureProduction(State->EnemyCommander, Producer, EUnitRole::Frontline, false);
		Producer->SetActorTickEnabled(false);
	}
	if (Proof == EJevWorldProof::ClaimedFallback)
		PlaceInHumanMain(State);
	State->EnemyCommander->Resources = 0;
	Planner->EvaluatePlan();
	if (!Plan(State, 0) || !Plan(State, 1))
		return false; // Dynamic navigation may reject the initial real commands.
	if (!PublishedMatches(State, Now))
		return true;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		Initial[Index] = *Plan(State, Index);
		InitialSerial[Index] = Forces[Index]->OrderSerial;
		if (!Check(FMath::IsNearlyEqual(Initial[Index].CommittedUntil - Now, 25.f, .01f),
				TEXT("Every accepted initial plan starts a full 25-second server-time commitment")))
			return true;
	}
	if (!Check(Initial[0].TicketNumber != Initial[1].TicketNumber,
			TEXT("Independent forces publish distinct tickets")))
		return true;
	if ((Proof == EJevWorldProof::TargetDestroyed || Proof == EJevWorldProof::ForeignAttack)
		&& !Check(Initial[0].Verb == EForceVerb::Attack && IsValid(Initial[0].TargetStructure)
				&& Cast<ACommandBuilding>(Initial[0].TargetStructure),
			TEXT("Destruction proof must start with an accepted concrete hostile building Attack")))
		return true;
	if ((Proof == EJevWorldProof::Escalation || Proof == EJevWorldProof::RejectedOrder)
		&& !Check(Initial[0].TargetRegionIndex != Initial[0].SourceRegionIndex,
			TEXT("Escalation proof must start with a committed order away from its source")))
		return true;
	if (Proof == EJevWorldProof::RejectedOrder || Proof == EJevWorldProof::CommittedClaims || Proof == EJevWorldProof::ClaimedFallback)
		for (const TWeakObjectPtr<AArmyGroup>& Force : Forces)
			Park(*Force);
	if (Proof == EJevWorldProof::ForeignAttack)
		for (const TWeakObjectPtr<AArmyGroup>& Force : Forces)
			for (AArmyUnit* Unit : Force->GetUnits())
				Unit->NextAttackTime = TNumericLimits<float>::Max();
	AcceptedAt = Now;
	Stage = 2;
	return false;
}

bool FJevPlannerWorldScenario::Progress(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now)
{
	if (Stage == 2 && Now - AcceptedAt < (Proof == EJevWorldProof::Commitment ? 2.f : .25f))
	{
		Planner->EvaluatePlan();
		return !PublishedMatches(State, Now) || !Unchanged(State, 0) || !Unchanged(State, 1);
	}
	if (Stage == 2 && Proof == EJevWorldProof::ForeignAttack
		&& ArmyTestSetup::CurrentRegion(Forces[0].Get()) != Initial[0].TargetRegionIndex)
	{
		if (Now >= Initial[0].CommittedUntil - 2.f)
			return Fail(TEXT("Real Attack must enter its player-owned target before commitment expires"));
		Planner->EvaluatePlan();
		return !PublishedMatches(State, Now) || !Unchanged(State, 0) || !Unchanged(State, 1);
	}
	if (Stage == 2)
		return Stage2(World, State, PC, Now);
	return AfterSetup(World, State, PC, Now);
}

bool FJevPlannerWorldScenario::Stage2(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now)
{
	if (Proof == EJevWorldProof::Commitment || Proof == EJevWorldProof::RejectedOrder)
	{
		if (PrepareHealth(World, State, PC))
			return true;
	}
	else if (Proof == EJevWorldProof::Escalation || Proof == EJevWorldProof::ForeignAttack)
	{
		if (PrepareInvasion(World, State, PC))
			return true;
	}
	else if (Proof == EJevWorldProof::CommittedClaims)
	{
		if (PrepareClaims(State))
			return true;
	}
	else if (Proof != EJevWorldProof::ClaimedFallback && PrepareDestroyed())
		return true;
	Stage = 3;
	return AfterSetup(World, State, PC, Now);
}

bool FJevPlannerWorldScenario::AfterSetup(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now)
{
	if (Proof == EJevWorldProof::RejectedOrder && Now >= Initial[0].CommittedUntil && Stage == 3)
	{
		AArmyGroup* Force = Forces[0].Get();
		const int32 Number = Force->ForceNumber;
		Force->Initialize({ 0, State->EnemyCommander.Get(), Force->GetArmyIndex(),
			Force->GetProductionBuilding(), Force->GetHomeLocation() });
		Force->ForceNumber = Number;
		if (!Check(!FCommandService::IssueForceOrder(State->EnemyCommander, Force, EForceVerb::MoveHold,
					   ArmyTestSetup::CurrentRegion(Force))
					&& Force->OrderSerial == InitialSerial[0],
				TEXT("Real command service rejects mismatched team identity without changing the live accepted order")))
			return true;
		RejectedAt = Now;
		Stage = 4;
	}
	return Evaluate(World, State, PC, Now);
}

bool FJevPlannerWorldScenario::Evaluate(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, float Now)
{
	Planner->EvaluatePlan();
	if (!PublishedMatches(State, Now))
		return true;
	if (Proof == EJevWorldProof::ClaimedFallback)
		return ClaimedFallback(State, Now);
	if (Proof == EJevWorldProof::RejectedOrder)
		return RejectedOrder(World, State, PC, Now);
	if (Proof == EJevWorldProof::Commitment)
		return Commitment(State, Now);
	if (!Unchanged(State, 1))
		return true;
	const FJevPublishedPlan* Changed = Plan(State, 0);
	if (Proof == EJevWorldProof::ForeignAttack)
		return ForeignAttack(State, Now, Changed);
	if (Proof == EJevWorldProof::CommittedClaims)
		return CommittedClaims(State, Now, Changed);
	if (Proof == EJevWorldProof::Escalation)
		return Escalation(World, State, PC, Now, Changed);
	return TargetDestroyed(State, Now, Changed);
}

bool FJevPlannerWorldScenario::Begin(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC)
{
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		It->Destroy();
	for (TActorIterator<ACommandBuilding> It(World); It; ++It)
		It->Destroy();
	State->bVerificationIncomePaused = true;
	State->EnemyCommander->Resources = 240; // Exactly two six-infantry paid setup rosters; zero during intent proof.
	for (TActorIterator<ACapturePoint> It(World); It; ++It)
		It->SetActorTickEnabled(false); // Incidental capture would legally invalidate a held expansion target.
	const int32 Home = ArmyTestSetup::RegionAt(State, State->EnemyHeadquarters->GetActorLocation());
	const AMapRegion* HomeRegion = State->FindRegionAt(State->EnemyHeadquarters->GetActorLocation());
	AMapRegion* ForwardSource = nullptr;
	for (AMapRegion* Region : State->Regions)
		if (IsValid(Region) && Region->RegionRole != ERegionRole::Main && IsValid(Region->Anchor)
			&& HomeRegion->Neighbours.Contains(Region->RegionIndex)
			&& (!ForwardSource || Region->RegionIndex < ForwardSource->RegionIndex))
			ForwardSource = Region;
	if (!ForwardSource)
		return Fail(TEXT("Generated enemy main needs an anchored neighbour for independent source fixtures"));
	for (AMapRegion* Region : State->Regions)
		if (IsValid(Region) && IsValid(Region->Anchor))
			Region->Anchor->ControllingTeam = -1;
	ForwardSource->Anchor->ControllingTeam = 5;
	return BeginForces(World, State, PC, Home, ForwardSource);
}

bool FJevPlannerWorldScenario::BeginForces(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, int32 Home, AMapRegion* ForwardSource)
{
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AArmyGroup* Guard = ArmyTestSetup::SpawnGroup(World, PC, 40 + Index,
			ArmyTestSetup::FromFriendlyHQ(State, -250.f, Index ? 500.f : -500.f, 100.f));
		if (!Guard)
			return Fail(TEXT("Passive home guards must prevent empty-opponent HQ advantage"));
		Park(*Guard);
		const int32 Source = Index ? ForwardSource->RegionIndex : Home;
		const FVector Anchor = State->GetRegionAnchor(Source);
		const FVector Assembly = Anchor + FVector(Index ? 0.f : -600.f, 0.f, 100.f);
		ACommandBuilding* Producer = SpawnBuilding(World, State->EnemyCommander, ArmyTestSetup::BarracksIndex,
			Anchor + FVector(0.f, -800.f, 5.f));
		if (!Producer)
			return Fail(TEXT("Explicit completed producer fixture must spawn"));
		const FTransform Transform(Assembly);
		AArmyGroup* Force = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Force)
			return Fail(TEXT("Independent producer force allocation must succeed"));
		Force->Initialize({ 5, State->EnemyCommander.Get(), Index, Producer, Transform.GetLocation() });
		Force->FinishSpawning(Transform);
		Producer->ForceGroup = Force;
		Producer->ProductionUnitIndex = ArmyTestSetup::UnitIndex(State, EUnitRole::Frontline);
		Producer->ProductionRole = EUnitRole::Frontline;
		Producer->bForceConfigured = true;
		if (!FCommandService::SetRallyPoint(State->EnemyCommander, Producer, Source)
			|| !FCommandService::ConfigureProduction(State->EnemyCommander, Producer, EUnitRole::Frontline, true))
			return Fail(TEXT("Independent producer fixtures must accept normal rally and paid production commands"));
		Forces[Index] = Force;
	}
	return BeginPlanner(World, State, PC, ForwardSource);
}

bool FJevPlannerWorldScenario::BeginPlanner(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, AMapRegion* ForwardSource)
{
	if (Proof == EJevWorldProof::TargetDestroyed || Proof == EJevWorldProof::ForeignAttack)
		for (AMapRegion* Region : State->Regions)
			if (IsValid(Region) && Region->RegionRole != ERegionRole::Main && IsValid(Region->Anchor)
				&& Region != ForwardSource)
			{
				Region->Anchor->ControllingTeam = 0;
				if (!SpawnBuilding(World, PC->GetPlayerState<ACommandPlayerState>(), ArmyTestSetup::WorkshopIndex,
						State->GetRegionAnchor(Region->RegionIndex) + FVector(0.f, 700.f, 5.f)))
					return Fail(TEXT("Concrete hostile target fixtures must spawn outside anchor arrival footprints"));
			}
	if (Proof == EJevWorldProof::ClaimedFallback)
		for (AMapRegion* Region : State->Regions)
			if (IsValid(Region) && Region->RegionRole != ERegionRole::Main && IsValid(Region->Anchor))
				Region->Anchor->ControllingTeam = 5; // No neutral region is left: the player's main is the only target.
	if (!Templates.Load())
		return Fail(TEXT("World proof must load the real writer-authored memo templates"));
	Planner = World->SpawnActor<AEnemyCommander>();
	if (!Planner.IsValid())
		return Fail(TEXT("Isolated real planner must spawn"));
	Planner->SetActorTickEnabled(false); // Explicit bounded evaluations, no competing ordinary AI.
	Stage = 1;
	return false;
}

#endif
