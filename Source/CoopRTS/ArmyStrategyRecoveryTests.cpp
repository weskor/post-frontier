#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyStrategyFixture.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"

bool FEnemyConstructionScenario::Stage5(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, AEnemyCommander* Planner)
{
	if (ArmyTestSetup::GameSeconds(World) < DefenseReadyAt)
		return false; // A remote region threat does not override another force's active commitment.
	return PrepareRecovery(Planner, State, PC, World);
}
bool FEnemyConstructionScenario::Stage3(UWorld* World, ACommandGameState* State, AEnemyCommander* Planner)
{
	if (ArmyTestSetup::GameSeconds(World) < RecoveryReadyAt)
		return false; // Joined-health scoring cannot replace a still-valid committed order.
	for (const TWeakObjectPtr<AArmyUnit>& Unit : DamagedUnits)
		if (Unit.IsValid())
			Unit->SetActorTickEnabled(true);
	Planner->EvaluatePlan();
	const bool bSafe = InSafeRecovery(State);
	if (!bSafe && Recovery->Verb != EForceVerb::Retreat)
		return Fail(TEXT("Below-35-percent joined health must issue Retreat before recovery movement"));
	if (!bSafe)
		return false; // Await the real force tick applying its route after navigation becomes ready.
	SafeRecoveryRegion = Recovery->WaypointRegionIndex;
	if (Recovery->GetProductionBuilding() != Production.Get() || Production->ForceGroup != Recovery.Get())
		return Fail(TEXT("Retreat cannot detach or replace the injured force's owning producer"));
	if (OtherForce->Verb == EForceVerb::Retreat)
		return Fail(TEXT("Damage to the original producer cannot retreat the healthy forward producer"));
	Planner->SetActorTickEnabled(false); // Explicit evaluations below observe natural return and repair ticks.
	Stage = 4;
	return false;
}
bool FEnemyConstructionScenario::Stage4(UWorld* World, ACommandGameState* State, AEnemyCommander* Planner)
{
	if (!Recovery.IsValid() || !Production.IsValid() || Production->ForceGroup != Recovery.Get()
		|| Recovery->GetProductionBuilding() != Production.Get())
		return Fail(TEXT("Recovery must retain its living producer and stable force backlink"));
	float Health = 0.f;
	int32 Joined = 0;
	for (const TWeakObjectPtr<AArmyUnit>& Member : DamagedUnits)
	{
		const AArmyUnit* Unit = Member.Get();
		if (!IsValid(Unit) || !Unit->IsAlive() || Unit->GetGroup() != Recovery.Get())
			return Fail(TEXT("Natural recovery must heal the same joined recruits, not replace or transfer them"));
		Health += float(Unit->GetHealth()) / Unit->MaxHealth();
		++Joined;
	}
	if (Recovery->Verb == EForceVerb::MoveHold && Recovery->Status == EForceStatus::Holding
		&& InSafeRecovery(State))
		bObservedSafeHold = true;
	if (!bRecoveryObjectiveOpened && Joined && Health / Joined >= .8f && HealedTicket
		&& ArmyTestSetup::GameSeconds(World) >= HealedDeadline)
	{
		// Recovery cannot steal another force's committed goal. Expose a
		// genuinely unclaimed regional objective at the decision boundary.
		AMapRegion* Objective = nullptr;
		for (AMapRegion* Region : State->Regions)
			if (IsValid(Region) && IsValid(Region->Anchor) && Region->RegionRole != ERegionRole::Main
				&& Region->RegionIndex != SafeRecoveryRegion && State->GetRegionController(Region->RegionIndex) == 5
				&& !State->EnemyPlans.ContainsByPredicate([&](const FJevPublishedPlan& Plan) {
					   return Plan.TargetRegionIndex == Region->RegionIndex;
				   })
				&& (!Objective || Region->RegionIndex < Objective->RegionIndex))
				Objective = Region;
		if (!Objective)
			return Fail(TEXT("Recovery resumption fixture needs a controlled regional objective with no other committed destination"));
		Objective->Anchor->SetActorTickEnabled(false);
		Objective->Anchor->ControllingTeam = INDEX_NONE;
		bRecoveryObjectiveOpened = true;
		Test->AddInfo(FString::Printf(TEXT("Reopened unclaimed recovery objective %d at expired commitment %.2f"),
			Objective->RegionIndex, HealedDeadline));
	}
	return EvaluateRecovery(World, State, Planner, Health, Joined);
}
bool FEnemyConstructionScenario::EvaluateRecovery(UWorld* World, ACommandGameState* State, AEnemyCommander* Planner, float Health, int32 Joined)
{
	Planner->EvaluatePlan();
	if (!OtherProduction.IsValid() || !OtherForce.IsValid() || OtherProduction->ForceGroup != OtherForce.Get()
		|| OtherForce->GetProductionBuilding() != OtherProduction.Get())
		return Fail(TEXT("Independent producer must retain its own living force during another producer's recovery"));
	if (OtherForce->Verb == EForceVerb::Retreat)
		return Fail(TEXT("Recovering one force cannot retreat an uninjured independent producer"));
	if (!Joined || Health / Joined < .8f)
	{
		if (!InSafeRecovery(State) || Recovery->WaypointRegionIndex != SafeRecoveryRegion)
			return Fail(*FString::Printf(TEXT("Injured force must retain its chosen controlled safe waypoint while retreating or physically held below 80 percent joined health (verb=%d status=%d waypoint=%d target=%d safe=%d center=%s destination=%s)"),
				static_cast<int32>(Recovery->Verb), static_cast<int32>(Recovery->Status),
				Recovery->WaypointRegionIndex, Recovery->TargetRegionIndex, SafeRecoveryRegion,
				*Recovery->GetCenter().ToString(), *Recovery->Destination.ToString()));
		return false;
	}
	if (!bObservedSafeHold || Health / Joined <= InitialDamagedHealth)
		return Fail(TEXT("Recovery must physically reach safe MoveHold and heal the same injured joined roster before resuming"));
	return ResumeRecovery(World, State, Health, Joined);
}
bool FEnemyConstructionScenario::ResumeRecovery(UWorld* World, ACommandGameState* State, float Health, int32 Joined)
{
	const FJevPublishedPlan* HeldPlan = PublishedRecovery(State);
	if (Recovery->TargetRegionIndex == SafeRecoveryRegion && HeldPlan
		&& ArmyTestSetup::GameSeconds(World) < (HealedTicket ? HealedDeadline : HeldPlan->CommittedUntil))
	{
		if (HealedTicket == 0)
		{
			HealedTicket = HeldPlan->TicketNumber;
			HealedDeadline = HeldPlan->CommittedUntil;
		}
		if (HeldPlan->TicketNumber != HealedTicket || HeldPlan->CommittedUntil != HealedDeadline)
			return Fail(*FString::Printf(TEXT("Healing cannot replace a safe recovery ticket before its recorded deadline (old=%d deadline=%.2f new=%d newdeadline=%.2f now=%.2f target=%d owner=%d escalated=%d)"),
				HealedTicket, HealedDeadline, HeldPlan->TicketNumber, HeldPlan->CommittedUntil,
				ArmyTestSetup::GameSeconds(World), HeldPlan->TargetRegionIndex,
				State->GetRegionController(HeldPlan->TargetRegionIndex), HeldPlan->bEscalated));
		return false;
	}
	if ((Recovery->Verb != EForceVerb::MoveHold && Recovery->Verb != EForceVerb::Attack)
		|| Recovery->Orders.IsEmpty() || Recovery->WaypointRegionIndex == INDEX_NONE
		|| Recovery->TargetRegionIndex == SafeRecoveryRegion)
		return Fail(*FString::Printf(TEXT("Healed roster has no resumed regional travel (health=%.3f oldticket=%d olddeadline=%.2f ticket=%d deadline=%.2f now=%.2f verb=%d target=%d safe=%d source=%d owner=%d escalated=%d memo=%s)"),
			Health / Joined, HealedTicket, HealedDeadline, HeldPlan ? HeldPlan->TicketNumber : 0,
			HeldPlan ? HeldPlan->CommittedUntil : 0.f, ArmyTestSetup::GameSeconds(World),
			int32(Recovery->Verb), Recovery->TargetRegionIndex, SafeRecoveryRegion,
			ArmyTestSetup::CurrentRegion(Recovery.Get()), State->GetRegionController(SafeRecoveryRegion),
			HeldPlan && HeldPlan->bEscalated, HeldPlan ? *HeldPlan->Memo : TEXT("missing")));
	return ObserveResumedTravel(State, World);
}
const FJevPublishedPlan* FEnemyConstructionScenario::PublishedRecovery(const ACommandGameState* State) const
{
	return State->EnemyPlans.FindByPredicate(
		[&](const FJevPublishedPlan& Plan) { return Plan.Force == Recovery.Get(); });
}
bool FEnemyConstructionScenario::PrepareRecovery(AEnemyCommander* Planner, ACommandGameState* State, ACommandPlayerController* PC, UWorld* World)
{
	const AMapRegion* ForwardRegion = ForwardProduction.IsValid()
		? State->FindRegionAt(ForwardProduction->GetActorLocation())
		: nullptr;
	if (!ForwardRegion || State->GetRegionController(ForwardRegion->RegionIndex) != 5)
		return Fail(TEXT("Forward defense fixture must retain its naturally captured controlled region"));
	const FVector ThreatAnchor = State->GetRegionAnchor(ForwardRegion->RegionIndex);
	ACommandBuilding* NearestDefender = nullptr;
	float NearestDistance = TNumericLimits<float>::Max();
	for (ACommandBuilding* Producer : State->Buildings)
	{
		if (!IsValid(Producer) || Producer->TeamIndex != 5 || !Producer->IsProducer()
			|| !Producer->IsComplete() || !IsValid(Producer->ForceGroup))
			continue;
		const float Distance = FVector::DistSquared2D(Producer->ForceGroup->GetCenter(), ThreatAnchor);
		if (Distance < NearestDistance)
		{
			NearestDistance = Distance;
			NearestDefender = Producer;
		}
	}
	AArmyGroup* Threat = ArmyTestSetup::SpawnGroup(World, PC, 20, ThreatAnchor + FVector(0.f, 0.f, 100.f));
	if (!Threat)
		return Fail(TEXT("Real hostile-pressure fixture could not spawn"));
	if (!State->IsRegionContested(ForwardRegion->RegionIndex, 5))
		return Fail(TEXT("Defense fixture must place real hostile units inside the captured forward region"));
	Planner->EvaluatePlan();
	if (!NearestDefender || NearestDefender->ForceGroup->Verb != EForceVerb::MoveHold
		|| NearestDefender->ForceGroup->TargetRegionIndex != ForwardRegion->RegionIndex)
		return Fail(TEXT("Nearest producer force must receive MoveHold on the actually threatened controlled region"));
	if (NearestDefender->ForceGroup->Orders.IsEmpty())
		return Fail(TEXT("Threatened-region defense must retain an accepted active force order"));
	if (!Production.IsValid() || Production->ForceGroup != Recovery.Get()
		|| Recovery->GetProductionBuilding() != Production.Get())
		return Fail(TEXT("Recovery fixture must use the real producer-owned force"));
	int32 DamageJoined, DamageTravelling;
	Production->GetForceCounts(DamageJoined, DamageTravelling);
	if (DamageJoined == 0 || DamageTravelling != 0)
		return Fail(TEXT("Recovery damage fixture requires naturally joined recruits and no healthy arrivals in flight"));
	return DamageRecovery(State, Threat, DamageJoined);
}
bool FEnemyConstructionScenario::DamageRecovery(ACommandGameState* State, AArmyGroup* Threat, int32 DamageJoined)
{
	float DamagedHealth = 0.f;
	for (AArmyUnit* Unit : Recovery->GetUnits())
		if (IsValid(Unit) && Unit->IsAlive())
		{
			Unit->ReceiveAttack(Unit->GetHealth() - FMath::Max(1, Unit->MaxHealth() / 4), Threat->GetUnits()[0]);
			DamagedHealth += float(Unit->GetHealth()) / Unit->MaxHealth();
			DamagedUnits.Add(Unit);
			// Keep the below-threshold roster intact while its previous order is
			// committed; natural FieldRepairs starts when Retreat may be chosen.
			Unit->SetActorTickEnabled(false);
		}
	if (DamagedUnits.Num() != DamageJoined || DamagedHealth / DamageJoined >= .35f)
		return Fail(TEXT("Real hostile damage must reduce the observed joined roster below the 35 percent recovery threshold"));
	InitialDamagedHealth = DamagedHealth / DamageJoined;
	Threat->Destroy();
	const FJevPublishedPlan* RecoveryPlan = PublishedRecovery(State);
	if (!RecoveryPlan)
		return Fail(TEXT("Damaged producer must retain its published accepted commitment before recovery"));
	RecoveryReadyAt = RecoveryPlan->CommittedUntil;
	State->EnemyCommander->Resources = 2000; // Paid repairs setup, not asserted income.
	OtherProduction = ForwardProduction;
	OtherForce = OtherProduction->ForceGroup;
	ACommandBuilding* Workshop = nullptr;
	for (ACommandBuilding* Building : State->Buildings)
		if (IsValid(Building) && Building->TeamIndex == 5 && Building->Kind == EBuildingKind::Workshop)
			Workshop = Building;
	if (!Workshop)
		Workshop = PlaceEnemy(State, TEXT("workshop"), State->EnemyHeadquarters->GetActorLocation());
	if (!Workshop)
		return Fail(TEXT("Recovery research fixture has no legal workshop footprint"));
	Workshop->Tick(60.f);
	const int32 ResearchBefore = State->EnemyCommander->Resources;
	if (State->EnemyCommander->Doctrine == EArmyDoctrine::None)
	{
		if (!FCommandService::Research(State->EnemyCommander, Workshop, EArmyDoctrine::FieldRepairs)
			|| State->EnemyCommander->Resources != ResearchBefore - ACommandBuilding::ResearchCost)
			return Fail(TEXT("Recovery research must pay exactly once from JEV wallet"));
	}
	else if (State->EnemyCommander->Doctrine != EArmyDoctrine::FieldRepairs
		|| FCommandService::Research(State->EnemyCommander, Workshop, EArmyDoctrine::FieldRepairs).IsAccepted() || State->EnemyCommander->Resources != ResearchBefore)
		return Fail(TEXT("Naturally purchased repairs must stay locked and reject a second charge"));
	Stage = 3;
	return false;
}
bool FEnemyConstructionScenario::ObserveResumedTravel(const ACommandGameState* State, const UWorld* World)
{
	const int32 Waypoint = Recovery->WaypointRegionIndex;
	const AMapRegion* Region = State->FindRegionAt(Recovery->Destination);
	if (!Region || Region->RegionIndex != Waypoint)
		return Fail(TEXT("Resumed force destination must remain inside its actual region waypoint"));
	const FVector Center = Recovery->GetCenter();
	if (ResumeDeadline < 0.)
	{
		ResumeStart = Center;
		ResumeTarget = Recovery->Destination;
		ResumeDeadline = FPlatformTime::Seconds() + 10.;
	}
	if (FVector::Dist2D(Center, ResumeTarget) > FVector::Dist2D(ResumeStart, ResumeTarget) - 100.f)
		return FPlatformTime::Seconds() >= ResumeDeadline
			? Fail(TEXT("Healed JEV force must physically leave recovery and advance toward its resumed waypoint within ten real seconds"))
			: false;
	Test->AddInfo(TEXT("Enemy proof: exact paid economy, real capture/extractor, forward production, independent producer safe recovery and physical resumed strategic travel."));
	return true;
}
bool FEnemyConstructionScenario::Fail(const TCHAR* Message)
{
	Test->AddError(Message);
	return true;
}
bool FEnemyConstructionScenario::InSafeRecovery(const ACommandGameState* State) const
{
	if (Recovery->WaypointRegionIndex == INDEX_NONE
		|| State->GetRegionController(Recovery->WaypointRegionIndex) != 5
		|| State->IsRegionContested(Recovery->WaypointRegionIndex, 5))
		return false;
	if (Recovery->Verb == EForceVerb::Retreat)
		return Recovery->TargetRegionIndex == INDEX_NONE
			&& (Recovery->Status == EForceStatus::Retreating || Recovery->Status == EForceStatus::Refilling)
			&& FVector::Dist2D(Recovery->Destination, State->GetRegionAnchor(Recovery->WaypointRegionIndex)) <= 75.f;
	if (Recovery->IsHoldingRegion())
	{
		const AMapRegion* Held = State->FindRegionAt(Recovery->GetCenter());
		return Recovery->HoldRegionIndex == Recovery->WaypointRegionIndex
			&& Recovery->TargetRegionIndex == Recovery->WaypointRegionIndex
			&& Held && Held->RegionIndex == Recovery->WaypointRegionIndex;
	}
	// Replacing the completed Retreat's producer rally starts MoveHold in Marching
	// until the next executor tick observes arrival. It must already be physically
	// at the same safe destination; a later real Holding phase is still required.
	const AMapRegion* PhysicalRegion = State->FindRegionAt(Recovery->GetCenter());
	return Recovery->Verb == EForceVerb::MoveHold
		&& (Recovery->Status == EForceStatus::Holding || Recovery->Status == EForceStatus::Marching)
		&& Recovery->TargetRegionIndex == Recovery->WaypointRegionIndex
		&& PhysicalRegion && PhysicalRegion->RegionIndex == Recovery->WaypointRegionIndex
		&& PhysicalRegion->Contains(Recovery->GetCenter())
		&& FVector::Dist2D(Recovery->Destination, State->GetRegionAnchor(Recovery->WaypointRegionIndex)) <= 75.f
		&& FVector::Dist2D(Recovery->GetCenter(), Recovery->Destination) <= 170.f;
}
ACommandBuilding* FEnemyConstructionScenario::PlaceEnemy(ACommandGameState* State, FName Id, const FVector& Center)
{
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Location = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			Location.Z = 5.f;
			Location = State->ResolveBuildingLocation(State->Content->BuildingIndexOf(Id), Location, 5);
			if (State->FindRegionAt(Location) != State->FindRegionAt(Center))
				continue;
			if (ACommandBuilding* Building = FCommandService::PlaceBuilding(State->EnemyCommander,
					State->Content->BuildingIndexOf(Id), Location)
					.Building)
				return Building;
		}
	return nullptr;
}
#endif
