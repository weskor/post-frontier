#include "EnemyCommander.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Headquarters.h"
#include "Engine/World.h"
#include "EngineUtils.h"

AEnemyCommander::AEnemyCommander()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = false;
}

void AEnemyCommander::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	if (!HasAuthority() || (EvaluateElapsed += DeltaSeconds) < 2.f) return;
	EvaluateElapsed = 0.f;
	EvaluatePlan();
}

ACommandBuilding* AEnemyCommander::BuildNear(ACommandGameState* State, EBuildingKind Kind, const FVector& Center)
{
	if (State->EnemyResources < ACommandBuilding::GetBuildCost(Kind)) return nullptr;
	// Deterministic candidate positions use the same collision, territory and
	// navigation validation as player construction; the planner cannot cheat.
	for (int32 Ring = 0; Ring < 4; ++Ring)
	{
		const float Radius = 360.f + Ring * 150.f;
		for (int32 Direction = 0; Direction < 12; ++Direction)
		{
			const float Angle = Direction * PI / 6.f;
			FVector Location = Center + FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.f);
			Location.Z = 5.f;
			FString Reason;
			if (ACommandBuilding* Building = State->TryPlaceBuilding(Kind, Location, nullptr, 5, Reason))
				return Building;
		}
	}
	return nullptr;
}

void AEnemyCommander::EvaluatePlan()
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters)) return;

	const FVector Home = State->EnemyHeadquarters->GetActorLocation();
	TArray<ACommandBuilding*, TInlineAllocator<8>> Barracks;
	ACommandBuilding* Workshop = nullptr;
	for (ACommandBuilding* Building : State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->TeamIndex != 5) continue;
		if (Building->Kind == EBuildingKind::Barracks) Barracks.Add(Building);
		if (Building->Kind == EBuildingKind::Workshop) Workshop = Building;
	}
	if (Barracks.IsEmpty())
	{
		BuildNear(State, EBuildingKind::Barracks, Home);
		State->EnemyPlan = TEXT("ESTABLISH BASE");
		State->EnemyPlanRationale = TEXT("Constructing paid production before deploying a persistent force");
		State->ForceNetUpdate();
		return;
	}

	int32 Frontline = 0;
	int32 Ranged = 0;
	int32 Siege = 0;
	int32 Intruders = 0;
	int32 FrontlineForces = 0, RangedForces = 0, SiegeForces = 0;
	for (const ACommandBuilding* Building : Barracks)
	{
		if (!Building->bForceConfigured) continue;
		if (Building->ProductionRole == EUnitRole::Frontline) ++FrontlineForces;
		else if (Building->ProductionRole == EUnitRole::Ranged) ++RangedForces;
		else ++SiegeForces;
	}
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		for (const AArmyUnit* Unit : It->Units)
		{
			if (!IsValid(Unit) || !Unit->IsAlive()) continue;
			if (Unit->TeamIndex == 0)
			{
				if (FVector::DistSquared2D(Unit->GetActorLocation(), Home) < FMath::Square(1500.f)) ++Intruders;
			}
			else if (Unit->TeamIndex == 5)
			{
				if (Unit->UnitRole == EUnitRole::Frontline) ++Frontline;
				else if (Unit->UnitRole == EUnitRole::Ranged) ++Ranged;
				else ++Siege;
			}
		}
	}

	int32 Established = 0;
	ACapturePoint* Target = nullptr;
	float BestScore = -TNumericLimits<float>::Max();
	for (ACapturePoint* Site : State->CaptureSites)
	{
		if (!IsValid(Site)) continue;
		if (Site->IsEstablishedForTeam(5)) { ++Established; continue; }
		if (Site->ControllingTeam == 5 && !Site->bFriendlyPresent)
		{
			bool bBuildingOutpost = false;
			for (ACommandBuilding* Building : State->Buildings)
				if (IsValid(Building) && Building->IsAlive() && Building->TeamIndex == 5
					&& Building->Kind == EBuildingKind::Outpost
					&& FVector::DistSquared2D(Building->GetActorLocation(), Site->GetActorLocation())
						< FMath::Square(ACapturePoint::TerritoryRadius))
					bBuildingOutpost = true;
			if (!bBuildingOutpost) BuildNear(State, EBuildingKind::Outpost, Site->GetActorLocation());
		}
		const float Score = (Site->ControllingTeam == 0 ? 3.f : 5.f)
			- FVector::Dist2D(Home, Site->GetActorLocation()) / 1200.f;
		if (Score > BestScore) { BestScore = Score; Target = Site; }
	}

	const float Now = GetWorld()->GetTimeSeconds();
	EFrontOrder Order = EFrontOrder::Secure;
	FVector Front = CommittedFront;
	if (Intruders)
	{
		Order = EFrontOrder::Defend;
		Front = Home + FVector(-400.f, -250.f, 0.f);
		CommitUntil = 0.f;
		State->EnemyPlan = TEXT("DEFEND BASE");
		State->EnemyPlanRationale = TEXT("Enemy presence near HQ interrupts expansion");
	}
	else if (Now >= CommitUntil || CommittedFront.IsNearlyZero())
	{
		const int32 AssaultStrength = ACommandBuilding::GetForceCapacity(EUnitRole::Frontline)
			+ ACommandBuilding::GetForceCapacity(EUnitRole::Ranged)
			+ ACommandBuilding::GetForceCapacity(EUnitRole::Siege);
		const bool bAssault = Established >= 2 || (Frontline + Ranged + Siege >= AssaultStrength);
		Front = bAssault || !Target ? State->FriendlyHeadquarters->GetActorLocation() : Target->GetActorLocation();
		CommittedFront = Front;
		CommitUntil = Now + 12.f;
		State->EnemyPlan = bAssault || !Target ? TEXT("ASSAULT HQ") : TEXT("EXPAND TERRITORY");
		State->EnemyPlanRationale = bAssault || !Target
			? TEXT("Established economy or assembled force supports the offensive")
			: TEXT("Secure a sector, establish an outpost, fund the next production line");
	}
	Front.Z = 5.f;
	for (ACommandBuilding* Building : Barracks)
	{
		if (!Building->IsComplete()) continue;
		// Composition decisions happen once per producer, not once per casualty or timer reset.
		const EUnitRole Role = Building->bForceConfigured ? Building->ProductionRole
			: FrontlineForces == 0 ? EUnitRole::Frontline : RangedForces == 0 ? EUnitRole::Ranged
			: SiegeForces == 0 ? EUnitRole::Siege : FrontlineForces <= RangedForces ? EUnitRole::Frontline : EUnitRole::Ranged;
		const bool bWasConfigured = Building->bForceConfigured;
		if (!Building->bProductionEnabled) Building->SetProduction(Role, true);
		if (!bWasConfigured && Building->bForceConfigured)
		{
			if (Role == EUnitRole::Frontline) ++FrontlineForces;
			else if (Role == EUnitRole::Ranged) ++RangedForces;
			else ++SiegeForces;
		}
		float Health = 0.f;
		int32 Living = 0;
		if (const AArmyGroup* Force = Building->ForceGroup; IsValid(Force))
			for (const AArmyUnit* Unit : Force->Units)
				if (IsValid(Unit) && Unit->IsAlive() && !Unit->bReinforcing)
				{
					Health += float(Unit->Health) / Unit->MaxHealth();
					++Living;
				}
		const bool bRecover = (Living > 0 && Health / Living < .35f)
			|| (Building->FrontOrder == EFrontOrder::FallBack && (!Living || Health / Living < .8f));
		const EFrontOrder Desired = bRecover ? EFrontOrder::FallBack : Order;
		const FVector Destination = bRecover ? Home + FVector(-500.f, 0.f, -Home.Z + 5.f) : Front;
		// Recovery changes only this producer's front. The stable force/backlink stays intact.
		if (!Building->HasConfiguredFront() || Building->FrontOrder != Desired || !Building->FrontLocation.Equals(Destination, 50.f))
			Building->SetFront(Desired, Destination);
	}

	// Reserve one full infantry force's replacement budget before optional investment.
	const int32 ReplacementReserve = ACommandBuilding::GetUnitCost(EUnitRole::Frontline)
		* ACommandBuilding::GetForceCapacity(EUnitRole::Frontline);
	if (!Intruders && Established > 0 && Barracks.Num() < 3
		&& State->EnemyResources >= ACommandBuilding::GetBuildCost(EBuildingKind::Barracks) + ReplacementReserve)
		BuildNear(State, EBuildingKind::Barracks, Home);
	else if (!Workshop && Established > 0
		&& State->EnemyResources >= ACommandBuilding::GetBuildCost(EBuildingKind::Workshop) + ReplacementReserve)
		BuildNear(State, EBuildingKind::Workshop, Home);
	else if (Workshop && Workshop->IsComplete() && State->EnemyDoctrine == EArmyDoctrine::None
		&& State->EnemyResources >= ACommandBuilding::ResearchCost + ReplacementReserve)
		Workshop->TryResearch(EArmyDoctrine::FieldRepairs);
	State->ForceNetUpdate();
}
