#include "CommandBuilding.h"

#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/BuildingDefinition.h"
#include "Content/MatchContent.h"
#include "Content/UnitDefinition.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "NavigationSystem.h"

namespace
{
bool GetBalance(const ACommandBuilding& Building, const ACommandGameState& State, int32& Balance)
{
	const ACommandPlayerState* Owner = Building.OwningPlayerState;
	if (!IsValid(Owner) || Owner->GetWorld() != Building.GetWorld() || Owner->TeamIndex != Building.TeamIndex
		|| (Building.TeamIndex == 0 ? Owner->CommanderIndex < 0 || Owner->CommanderIndex >= 5
									: Building.TeamIndex != 5 || Owner != State.EnemyCommander))
		return false;
	Balance = Owner->Resources;
	return true;
}

FProductionInput MakeProductionInput(const ACommandBuilding& Building, const ACommandGameState* State, float DeltaSeconds)
{
	FProductionInput In{};
	In.bMatchOngoing = State && State->MatchResult == EMatchResult::Ongoing;
	In.bProducer = Building.IsProducer();
	In.bComplete = Building.IsComplete();
	In.bAlive = Building.IsAlive();
	In.bConfigured = Building.bForceConfigured;
	In.bForceValid = IsValid(Building.ForceGroup);
	In.bEnabled = Building.bProductionEnabled;
	Building.GetForceCounts(In.Joined, In.Travelling);
	const UArmyUnitDefinition* Unit = Building.GetProductionDefinition();
	In.Capacity = Unit ? ACommandBuilding::GetForceCapacity(*Unit) : 0;
	In.bWalletValid = State && GetBalance(Building, *State, In.Balance);
	In.UnitCost = Building.GetProductionCost();
	In.Progress = Building.ProductionProgressSeconds;
	In.Duration = Building.GetProductionDuration();
	In.DeltaSeconds = DeltaSeconds;
	return In;
}

bool FindExit(const ACommandBuilding& Building, FVector& OutLocation, int32& Cursor)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Building.GetWorld());
	const UBuildingDefinition* Definition = Building.GetDefinition();
	if (!Navigation || !Definition)
		return false;
	const float Footprint = ACommandBuilding::GetFootprintRadius(*Definition);
	static const FVector Directions[] = {
		{ 1, 0, 0 }, { .707107, .707107, 0 }, { 0, 1, 0 }, { -.707107, .707107, 0 },
		{ -1, 0, 0 }, { -.707107, -.707107, 0 }, { 0, -1, 0 }, { .707107, -.707107, 0 }
	};
	const float Radius = Footprint + 240.f;
	while (Cursor < 24)
	{
		const int32 Candidate = Cursor++;
		const FVector Desired = Building.GetActorLocation()
			+ Directions[Candidate % 8] * (Radius + (Candidate / 8) * 145.f);
		FNavLocation Projected;
		if (!AArenaBounds::IsTravelLocation(Building.GetWorld(), Desired)
			|| !Navigation->ProjectPointToNavigation(Desired, Projected, FVector(45.f, 45.f, 200.f))
			|| !AArenaBounds::IsTravelLocation(Building.GetWorld(), Projected.Location)
			|| FVector::DistSquared2D(Desired, Projected.Location) > FMath::Square(45.f)
			|| FMath::Abs(Desired.Z - Projected.Location.Z) > 110.f
			|| FVector::DistSquared2D(Projected.Location, Building.GetActorLocation()) < FMath::Square(Footprint + 75.f)
			|| Building.GetWorld()->OverlapBlockingTestByChannel(Projected.Location + FVector(0.f, 0.f, 85.f),
				FQuat::Identity, ECC_Pawn, FCollisionShape::MakeCapsule(34.f, 60.f)))
			continue;
		OutLocation = Projected.Location;
		return true;
	}
	return false;
}

}
bool ACommandBuilding::FindProductionExit(FVector& OutLocation, int32& Cursor) const
{
	return FindExit(*this, OutLocation, Cursor);
}

void ACommandBuilding::GetForceCounts(int32& OutJoined, int32& OutTravelling) const
{
	OutJoined = OutTravelling = 0;
	if (!IsValid(ForceGroup))
		return;
	for (const AArmyUnit* Unit : ForceGroup->GetUnits())
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		if (Unit->IsReinforcing())
			++OutTravelling;
		else
			++OutJoined;
	}
}

EProductionState ACommandBuilding::GetProductionState() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return ProductionPolicy::Evaluate(MakeProductionInput(*this, State, 0.f)).State;
}

void ACommandBuilding::TickProduction(float DeltaSeconds)
{
	if (!HasAuthority() || !FMath::IsFinite(DeltaSeconds) || DeltaSeconds <= 0.f)
		return;
	FProductionInput In = MakeProductionInput(*this, GetWorld()->GetGameState<ACommandGameState>(), DeltaSeconds);
	In.bForceValid = In.bForceValid && !ForceGroup->IsActorBeingDestroyed();
	const FProductionDecision Decision = ProductionPolicy::Evaluate(In);
	if (Decision.State != EProductionState::Producing && Decision.State != EProductionState::DeploymentBlocked)
	{
		ProductionCheckAccumulator = 0.f;
		return;
	}
	ProductionCheckAccumulator += DeltaSeconds;
	ProductionProgressSeconds = Decision.NewProgress;
	if (!Decision.bDeploymentDue)
		return;
	if (ProductionCheckAccumulator < .25f)
		return;
	ProductionCheckAccumulator = 0.f;
	FVector Exit;
	int32 ExitCursor = 0;
	bool bDeployed = false;
	while (FindProductionExit(Exit, ExitCursor))
	{
		if (ForceGroup->SpawnReinforcement(ProductionUnitIndex, Exit))
		{
			bDeployed = true;
			break;
		}
	}
	if (!bDeployed)
		return;
	// Spawn and its accepted complete path precede the debit. A failed debit removes
	// only this new candidate; the persistent force and prior members are untouched.
	if (!TrySpend(GetProductionCost()))
	{
		ForceGroup->RollbackLastReinforcement();
		return;
	}
	++DeploymentCount;
	OnRep_DeploymentCount();
	ProductionProgressSeconds = 0.f;
	ForceNetUpdate();
}
