#include "ArmyGroup.h"

#include "AIController.h"
#include "ArenaBounds.h"
#include "ArmyGroupInternal.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/OrderGraph.h"
#include "Content/MatchContent.h"
#include "Engine/World.h"
#include "GroundHeight.h"
#include "MapRegion.h"
#include "NavigationSystem.h"

using namespace ArmyGroupInternal;

namespace
{
// A formation slot a recruit can stand on: navigable, in the arena and clear of other pawns.
// The floor height is the tagged ground under the slot (plateaus and ramps), not the navmesh sample.
bool FreeGround(UNavigationSystemV1& Navigation, const FVector& Wanted, FVector& Ground)
{
	UWorld* World = Navigation.GetWorld();
	FNavLocation Projected;
	if (!AArenaBounds::IsTravelLocation(World, Wanted)
		|| !Navigation.ProjectPointToNavigation(Wanted, Projected, FVector(75.f, 75.f, 200.f))
		|| FVector::DistSquared2D(Wanted, Projected.Location) > FMath::Square(75.f)
		|| !AArenaBounds::IsTravelLocation(World, Projected.Location))
		return false;
	const FVector Floor = GroundHeight::Snap(*World, Projected.Location);
	if (World->OverlapBlockingTestByChannel(Floor + FVector(0.f, 0.f, SpawnLift()),
			FQuat::Identity, ECC_Pawn, UnitCapsule()))
		return false;
	Ground = Floor;
	return true;
}
}

bool AArmyGroup::FormationSlotGoal(int32 Slot, FVector& Goal) const
{
	if (IsHoldingRegion() && HoldPostIndex != INDEX_NONE && !bHoldResponding)
	{
		// Holding slots are individually clipped/projected, not a rigid formation.
		Goal = HoldPostLocation + FormationOffset(Slot);
		return ClipHoldingDestination(Goal);
	}
	// A depleted formation's member center is biased toward its occupied slots.
	// Recover its moving anchor so an empty slot does not target an existing member.
	FVector Anchor = FVector::ZeroVector;
	int32 Joined = 0;
	for (const AArmyUnit* Member : Units)
	{
		if (!IsValid(Member) || !Member->IsAlive())
			continue;
		Anchor += Member->GetActorLocation() - FormationOffset(Member->CompositionSlot);
		++Joined;
	}
	if (Joined > 0)
		Anchor /= Joined;
	else
		Anchor = AppliedWaypoint != INDEX_NONE ? Destination : GetActorLocation();
	Goal = Anchor + FormationOffset(Slot);
	return ClipHoldingDestination(Goal);
}

void AArmyGroup::JoinFormation(AArmyUnit& Unit, AAIController& AI, UNavigationSystemV1& Navigation)
{
	// Adopt current intent. A refusal leaves the recruit standing in its slot; the next order tick
	// re-issues travel to every joined member that is not moving.
	if (Order == EArmyOrder::Hold)
	{
		AI.StopMovement();
		return;
	}
	FPreparedMove Formation;
	Formation.Controller = &AI;
	FVector FormationGoal = Destination + FormationOffset(Unit.CompositionSlot);
	if (ClipHoldingDestination(FormationGoal)
		&& PrepareMove(Navigation, Unit.GetNavAgentPropertiesRef(), &AI, *AI.GetPathFollowingComponent(),
			Unit.GetNavAgentLocation(), FormationGoal, Formation))
		StartPreparedMove(Formation);
}

bool AArmyGroup::QueueRecruit(int32 UnitIndex)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	const int32 Capacity = Definition ? ACommandBuilding::GetForceCapacity(*Definition) : 0;
	if (!CanAcceptRecruit(State, UnitIndex, Capacity) || GetAliveCount() == 0
		|| GetAliveCount() + PendingRecruits.Num() >= Capacity
		|| VacantReinforcementSlot(Units, UnitIndex, Capacity) == INDEX_NONE)
		return false;
	const int32 Cost = ProductionBuilding->GetProductionCost();
	if (!ProductionBuilding->TrySpend(Cost))
		return false;
	SupplyDelivery::FRecruit& Recruit = PendingRecruits.AddDefaulted_GetRef();
	Recruit.UnitIndex = UnitIndex;
	Recruit.Paid = Cost;
	// Start the delay now rather than at the next group tick.
	UpdateSupply();
	return true;
}

bool AArmyGroup::ClaimExitAttempt()
{
	const double Now = GetWorld()->GetTimeSeconds();
	if (!HasAuthority() || PendingRecruits.IsEmpty() || GetAliveCount() > 0 || Now < NextExitAttempt)
		return false;
	NextExitAttempt = Now + .25;
	return true;
}

bool AArmyGroup::SpawnRecruitForExit(const FVector& Exit)
{
	if (PendingRecruits.IsEmpty() || GetAliveCount() > 0 || !SpawnReinforcement(PendingRecruits[0].UnitIndex, Exit))
		return false;
	PendingRecruits.RemoveAt(0);
	SyncSupplyCounts(false);
	return true;
}

void AArmyGroup::CancelRecruits()
{
	const int32 Refund = SupplyDelivery::Refund(PendingRecruits);
	PendingRecruits.Reset();
	if (Refund > 0 && IsValid(OwningPlayerState))
		OwningPlayerState->AddResources(Refund);
	SyncSupplyCounts(false);
}

void AArmyGroup::SyncSupplyCounts(bool bCutOff)
{
	const SupplyDelivery::FCounts Counts = SupplyDelivery::Count(PendingRecruits);
	if (Counts.InTransit == RecruitsInTransit && Counts.Waiting == RecruitsWaiting && bCutOff == bSupplyCutOff)
		return;
	RecruitsInTransit = Counts.InTransit;
	RecruitsWaiting = Counts.Waiting;
	bSupplyCutOff = bCutOff;
	ForceNetUpdate();
}

int32 AArmyGroup::SupplyHops(const ACommandGameState& State, bool bForceEmpty)
{
	if (bForceEmpty)
		return INDEX_NONE;
	const int32 Here = ForceOrderGraph::SourceRegion(*this, State);
	if (Here != INDEX_NONE)
		LastSupplyRegion = Here;
	const AMapRegion* Producer = State.FindRegionAt(ProductionBuilding->GetActorLocation());
	uint64 Graph[ForceOrders::MaxRegions];
	const int32 Count = ForceOrderGraph::ReadGraph(State, Graph);
	return SupplyDelivery::Hops(Graph, Count, State.GetConnectedMask(TeamIndex),
		Producer ? Producer->RegionIndex : INDEX_NONE, LastSupplyRegion);
}

void AArmyGroup::UpdateSupply()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || !State)
		return;
	if (!IsValid(ProductionBuilding))
	{
		SyncSupplyCounts(false);
		return;
	}
	const bool bEmpty = GetAliveCount() == 0;
	const int32 Hops = SupplyHops(*State, bEmpty);
	const SupplyDelivery::ERoute Route = SupplyDelivery::Route(bEmpty, Hops);
	const double Now = GetWorld()->GetTimeSeconds();
	for (int32 Index = 0; Index < PendingRecruits.Num();)
	{
		const bool bArrived = SupplyDelivery::Advance(PendingRecruits[Index], Route, Hops, Now) == SupplyDelivery::EOutcome::Arrive;
		// A refused delivery stays due and is retried on the next tick.
		if (bArrived && DeliverRecruit(PendingRecruits[Index]))
			PendingRecruits.RemoveAt(Index);
		else
			++Index;
	}
	SyncSupplyCounts(Route == SupplyDelivery::ERoute::CutOff);
}

bool AArmyGroup::DeliverRecruit(const SupplyDelivery::FRecruit& Recruit)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(Recruit.UnitIndex) : nullptr;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const int32 Capacity = Definition ? ACommandBuilding::GetForceCapacity(*Definition) : 0;
	const int32 Slot = Definition ? VacantReinforcementSlot(Units, Recruit.UnitIndex, Capacity) : INDEX_NONE;
	if (!Navigation || Slot == INDEX_NONE || !CanAcceptRecruit(State, Recruit.UnitIndex, Capacity))
		return false;
	// The slot offsets depend on the produced shape; a refused delivery puts the old shape back.
	const bool bWasProduced = bProducedGroup;
	const int32 OldCapacity = ForceCapacity;
	bProducedGroup = true;
	ForceCapacity = Capacity;
	AArmyUnit* Unit = nullptr;
	FVector Goal;
	if (FormationSlotGoal(Slot, Goal))
	{
		// The slot itself, then the ground around it when something stands there.
		constexpr int32 Candidates = 9;
		for (int32 Attempt = 0; !Unit && Attempt < Candidates; ++Attempt)
		{
			const float Angle = 2.f * PI * (Attempt - 1) / (Candidates - 1);
			const FVector Wanted = Attempt == 0 ? Goal : Goal + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * 120.f;
			FVector Ground;
			if (FreeGround(*Navigation, Wanted, Ground))
				Unit = SpawnJoined(*Definition, Recruit.UnitIndex, Slot, Ground);
		}
	}
	if (!Unit)
	{
		bProducedGroup = bWasProduced;
		ForceCapacity = OldCapacity;
		return false;
	}
	if (AAIController* AI = GetReadyController(Unit))
		JoinFormation(*Unit, *AI, *Navigation);
	Unit->ForceNetUpdate();
	return true;
}
