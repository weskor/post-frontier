#include "EnemyCommander.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "Headquarters.h"
#include "Engine/World.h"
#include "EngineUtils.h"

namespace
{
	// Doctrine picks by capability and role; the catalogue decides which asset fulfils them.
	int32 FirstBuildingWith(const UMatchContent& Content, bool UBuildingDefinition::*Capability)
	{
		for (int32 Index = 0; Index < Content.Buildings.Num(); ++Index)
			if (const UBuildingDefinition* Building = Content.Building(Index); Building && Building->*Capability) return Index;
		return INDEX_NONE;
	}

	int32 FirstUnitWithRole(const UMatchContent& Content, EUnitRole Role)
	{
		for (int32 Index = 0; Index < Content.Units.Num(); ++Index)
			if (const UArmyUnitDefinition* Unit = Content.Unit(Index); Unit && Unit->Role == Role) return Index;
		return INDEX_NONE;
	}

	bool HasCapability(const ACommandBuilding& Building, bool UBuildingDefinition::*Capability)
	{
		const UBuildingDefinition* Definition = Building.GetDefinition();
		return Definition && Definition->*Capability;
	}
}

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

ACommandBuilding* AEnemyCommander::BuildNear(ACommandGameState* State, int32 BuildingIndex, const FVector& Center)
{
	const UBuildingDefinition* Definition = State->Content->Building(BuildingIndex);
	if (!Definition || State->EnemyResources < ACommandBuilding::GetBuildCost(*Definition)) return nullptr;
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
			if (ACommandBuilding* Building = State->TryPlaceBuilding(BuildingIndex, Location, nullptr, 5, Reason))
				return Building;
		}
	}
	return nullptr;
}

void AEnemyCommander::EvaluatePlan()
{
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || !State || !State->Content || State->MatchResult != EMatchResult::Ongoing
		|| !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters)) return;
	const UMatchContent& Content = *State->Content;
	const int32 ProducerIndex = FirstBuildingWith(Content, &UBuildingDefinition::bProducesForces);
	const int32 OutpostIndex = FirstBuildingWith(Content, &UBuildingDefinition::bEstablishesSector);
	const int32 WorkshopIndex = FirstBuildingWith(Content, &UBuildingDefinition::bOffersResearch);
	const int32 FrontlineIndex = FirstUnitWithRole(Content, EUnitRole::Frontline);
	const int32 RangedIndex = FirstUnitWithRole(Content, EUnitRole::Ranged);
	const int32 SiegeIndex = FirstUnitWithRole(Content, EUnitRole::Siege);
	if (ProducerIndex < 0 || FrontlineIndex < 0 || RangedIndex < 0 || SiegeIndex < 0) return;
	const UArmyUnitDefinition& Infantry = *Content.Unit(FrontlineIndex);

	const FVector Home = State->EnemyHeadquarters->GetActorLocation();
	TArray<ACommandBuilding*, TInlineAllocator<8>> Barracks;
	ACommandBuilding* Workshop = nullptr;
	for (ACommandBuilding* Building : State->Buildings)
	{
		if (!IsValid(Building) || !Building->IsAlive() || Building->TeamIndex != 5) continue;
		if (Building->IsProducer()) Barracks.Add(Building);
		if (HasCapability(*Building, &UBuildingDefinition::bOffersResearch)) Workshop = Building;
	}
	if (Barracks.IsEmpty())
	{
		BuildNear(State, ProducerIndex, Home);
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
		if (Site->ControllingTeam == 5 && !Site->bFriendlyPresent && OutpostIndex >= 0)
		{
			bool bBuildingOutpost = false;
			for (ACommandBuilding* Building : State->Buildings)
				if (IsValid(Building) && Building->IsAlive() && Building->TeamIndex == 5
					&& HasCapability(*Building, &UBuildingDefinition::bEstablishesSector)
					&& FVector::DistSquared2D(Building->GetActorLocation(), Site->GetActorLocation())
						< FMath::Square(ACapturePoint::TerritoryRadius))
					bBuildingOutpost = true;
			if (!bBuildingOutpost) BuildNear(State, OutpostIndex, Site->GetActorLocation());
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
		const int32 AssaultStrength = ACommandBuilding::GetForceCapacity(Infantry)
			+ ACommandBuilding::GetForceCapacity(*Content.Unit(RangedIndex))
			+ ACommandBuilding::GetForceCapacity(*Content.Unit(SiegeIndex));
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
		const int32 UnitIndex = Building->bForceConfigured ? Building->ProductionUnitIndex
			: Role == EUnitRole::Frontline ? FrontlineIndex : Role == EUnitRole::Ranged ? RangedIndex : SiegeIndex;
		const bool bWasConfigured = Building->bForceConfigured;
		if (!Building->bProductionEnabled) Building->SetProduction(UnitIndex, true);
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
	const int32 ReplacementReserve = ACommandBuilding::GetUnitCost(Infantry) * ACommandBuilding::GetForceCapacity(Infantry);
	const UBuildingDefinition* WorkshopDefinition = Content.Building(WorkshopIndex);
	if (!Intruders && Established > 0 && Barracks.Num() < 3
		&& State->EnemyResources >= ACommandBuilding::GetBuildCost(*Content.Building(ProducerIndex)) + ReplacementReserve)
		BuildNear(State, ProducerIndex, Home);
	else if (!Workshop && Established > 0 && WorkshopDefinition
		&& State->EnemyResources >= ACommandBuilding::GetBuildCost(*WorkshopDefinition) + ReplacementReserve)
		BuildNear(State, WorkshopIndex, Home);
	else if (Workshop && Workshop->IsComplete() && State->EnemyDoctrine == EArmyDoctrine::None
		&& State->EnemyResources >= ACommandBuilding::ResearchCost + ReplacementReserve)
		Workshop->TryResearch(EArmyDoctrine::FieldRepairs);
	State->ForceNetUpdate();
}
