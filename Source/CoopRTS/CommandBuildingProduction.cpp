#include "CommandBuilding.h"

#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "ArmyGroupInternal.h"
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
	// An upgrade pauses the building's production like an explicit pause, so every reader sees one state.
	In.bEnabled = Building.bProductionEnabled && !BranchPolicy::PausesProduction(Building.Branch.Phase);
	Building.GetForceCounts(In.Joined, In.Travelling);
	In.Waiting = IsValid(Building.ForceGroup) ? Building.ForceGroup->RecruitsWaiting : 0;
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
			|| Building.GetWorld()->OverlapBlockingTestByChannel(Projected.Location + FVector(0.f, 0.f, ArmyGroupInternal::ExitProbeLift()),
				FQuat::Identity, ECC_Pawn, ArmyGroupInternal::UnitCapsule()))
			continue;
		OutLocation = Projected.Location;
		return true;
	}
	return false;
}

// Offers each free exit in turn to the spawner until it accepts one.
bool DeployAtExit(const ACommandBuilding& Building, TFunctionRef<bool(const FVector&)> Spawn)
{
	FVector Exit;
	int32 Cursor = 0;
	while (FindExit(Building, Exit, Cursor))
		if (Spawn(Exit))
			return true;
	return false;
}

// Spawn first, debit second: a failed debit removes only this new candidate, never the force's members.
bool SpawnPaidAtExit(ACommandBuilding& Building, const FVector& Exit)
{
	if (!Building.ForceGroup->SpawnReinforcement(Building.RecruitUnitIndex(), Exit))
		return false;
	if (Building.TrySpend(Building.GetProductionCost()))
		return true;
	Building.ForceGroup->RollbackLastReinforcement();
	return false;
}

}
bool ACommandBuilding::FindProductionExit(FVector& OutLocation, int32& Cursor) const
{
	return FindExit(*this, OutLocation, Cursor);
}

int32 ACommandBuilding::GetBranchUnitIndex() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && State->Content && bForceConfigured ? State->Content->BranchIndexOf(ProductionUnitIndex) : INDEX_NONE;
}

const UArmyUnitDefinition* ACommandBuilding::GetBranchDefinition() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State && State->Content ? State->Content->Unit(GetBranchUnitIndex()) : nullptr;
}

int32 ACommandBuilding::RecruitUnitIndex() const
{
	const int32 BranchIndex = Branch.Phase == EBranchPhase::Done ? GetBranchUnitIndex() : INDEX_NONE;
	return BranchIndex != INDEX_NONE ? BranchIndex : ProductionUnitIndex;
}

void ACommandBuilding::StartBranchUpgrade()
{
	Branch = { EBranchPhase::Upgrading, 0.f };
	ForceNetUpdate();
}

void ACommandBuilding::TickBranch(float DeltaSeconds)
{
	if (!IsUpgrading())
		return;
	const BranchPolicy::FUpgradeStep Step = BranchPolicy::Advance(Branch.ProgressSeconds, DeltaSeconds, IsStunned());
	Branch = { Step.bCompleted ? EBranchPhase::Done : EBranchPhase::Upgrading, Step.Progress };
	ForceNetUpdate();
}

void ACommandBuilding::GetForceCounts(int32& OutJoined, int32& OutTravelling) const
{
	OutJoined = OutTravelling = 0;
	if (!IsValid(ForceGroup))
		return;
	// Recruits in transit or waiting at the producer never count as joined strength.
	OutJoined = ForceGroup->GetAliveCount();
	OutTravelling = ForceGroup->GetPendingRecruitCount();
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
	TickBranch(DeltaSeconds);
	// A wiped force's paid recruits leave this exit whatever the production state: they are already paid for.
	if (IsValid(ForceGroup) && ForceGroup->ClaimExitAttempt())
		DeployAtExit(*this, [&](const FVector& Exit) { return ForceGroup->SpawnRecruitForExit(Exit); });
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
	if (!Decision.bDeploymentDue || ProductionCheckAccumulator < .25f)
		return;
	ProductionCheckAccumulator = 0.f;
	// A force with living members gets its recruit along the supply chain; an empty one at the exit.
	const bool bAccepted = ForceGroup->GetAliveCount() > 0
		? ForceGroup->QueueRecruit(RecruitUnitIndex())
		: DeployAtExit(*this, [&](const FVector& Exit) { return SpawnPaidAtExit(*this, Exit); });
	if (!bAccepted)
		return;
	++DeploymentCount;
	OnRep_DeploymentCount();
	ProductionProgressSeconds = 0.f;
	ForceNetUpdate();
}
