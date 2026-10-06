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

// The fitted slot of a force around Centre: inside RegionIndex, or inside the region that holds Centre when
// the index is INDEX_NONE. Outside every region the formation is its plain layout.
FVector FittedGoal(const ACommandGameState* State, int32 RegionIndex, const ArmyGroupPolicy::FFormation& Formation,
	const FVector& Centre, int32 Slot)
{
	const AMapRegion* Region = !State ? nullptr
		: RegionIndex != INDEX_NONE   ? ForceOrderGraph::Region(*State, RegionIndex)
									  : State->FindRegionAt(Centre);
	const TConstArrayView<FVector2D> Polygon = Region ? TConstArrayView<FVector2D>(Region->Polygon) : TConstArrayView<FVector2D>();
	return ArmyGroupPolicy::FittedSlot(Formation, ArmyGroupPolicy::FitForce(Formation, Polygon, Centre), Polygon, Slot);
}
}

bool AArmyGroup::FormationSlotGoal(int32 Slot, FVector& Goal) const
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (IsHoldingRegion() && HoldPostIndex != INDEX_NONE && !bHoldResponding)
	{
		// The idle post's slots are one rigid set fitted inside the region, as the holders stand in it.
		Goal = FittedGoal(State, HoldRegionIndex, FormationShape(), HoldPostLocation, Slot);
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
	Goal = FittedGoal(State, INDEX_NONE, FormationShape(), Anchor, Slot);
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
	// The slot the members of this intent stand in: around the idle post while holding, else around the destination
	// (a responding holder's destination is its threat, as before the fit).
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const bool bAtPost = IsHoldingRegion() && HoldPostIndex != INDEX_NONE && !bHoldResponding;
	FVector FormationGoal = FittedGoal(State, bAtPost ? HoldRegionIndex : INDEX_NONE, FormationShape(),
		bAtPost ? HoldPostLocation : Destination, Unit.CompositionSlot);
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
		|| VacantReinforcementSlot(Units, UnitIndex, ProductionBuilding->ProductionUnitIndex, Capacity) == INDEX_NONE)
		return false;
	const int32 Cost = ProductionBuilding->GetProductionCost();
	if (!ProductionBuilding->TrySpend(Cost))
		return false;
	SupplyDelivery::FRecruit& Recruit = PendingRecruits.AddDefaulted_GetRef();
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
	if (PendingRecruits.IsEmpty() || GetAliveCount() > 0 || !IsValid(ProductionBuilding)
		|| !SpawnReinforcement(ProductionBuilding->RecruitUnitIndex(), Exit))
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
		if (bArrived && DeliverRecruit())
			PendingRecruits.RemoveAt(Index);
		else
			++Index;
	}
	UpdateRefit(Route, Hops, Now);
	SyncSupplyCounts(Route == SupplyDelivery::ERoute::CutOff);
}

bool AArmyGroup::DeliverRecruit()
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	// The recruit takes the producer's current form: a branch bought while it travelled applies.
	const int32 UnitIndex = ProductionBuilding->RecruitUnitIndex();
	const UArmyUnitDefinition* Definition = State && State->Content ? State->Content->Unit(UnitIndex) : nullptr;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	const int32 Capacity = Definition ? ACommandBuilding::GetForceCapacity(*Definition) : 0;
	const int32 Slot = Definition ? VacantReinforcementSlot(Units, UnitIndex, ProductionBuilding->ProductionUnitIndex, Capacity) : INDEX_NONE;
	if (!Navigation || Slot == INDEX_NONE || !CanAcceptRecruit(State, UnitIndex, Capacity))
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
				Unit = SpawnJoined(*Definition, UnitIndex, Slot, Ground);
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

// Existing members refit one at a time through the same channel as a replacement: the lowest composition slot in
// the base form waits the delivery delay, then takes the branch's definition. A cut-off force restarts the delay
// and an orphan never gets here, so both keep the old form.
void AArmyGroup::UpdateRefit(SupplyDelivery::ERoute Route, int32 Hops, double Now)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const int32 BaseIndex = ProductionBuilding->ProductionUnitIndex;
	const int32 BranchIndex = ProductionBuilding->Branch.Phase == EBranchPhase::Done ? ProductionBuilding->GetBranchUnitIndex() : INDEX_NONE;
	const UArmyUnitDefinition* Branch = State && State->Content ? State->Content->Unit(BranchIndex) : nullptr;
	if (!Branch)
	{
		RefitSlot = INDEX_NONE;
		return;
	}
	TArray<BranchPolicy::FMember, TInlineAllocator<6>> Living;
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive())
			Living.Add({ Unit->GetCompositionSlot(), Unit->GetUnitIndex() });
	// The member in line may have died: the next one starts a fresh delay.
	if (!Living.ContainsByPredicate([&](const BranchPolicy::FMember& Member) { return Member.Slot == RefitSlot && Member.UnitIndex == BaseIndex; }))
	{
		RefitSlot = BranchPolicy::NextRefit(Living, BaseIndex);
		RefitTimer = {};
	}
	if (RefitSlot == INDEX_NONE || SupplyDelivery::Advance(RefitTimer, Route, Hops, Now) != SupplyDelivery::EOutcome::Arrive)
		return;
	for (AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetCompositionSlot() == RefitSlot)
			ApplyRefit(*Unit, BranchIndex, *Branch);
	RefitSlot = INDEX_NONE;
	RefitTimer = {};
}

// The unit keeps its place, target and the fraction of its HP and shield; only its definition changes, so every
// combat read sees the branch's stats from here on.
void AArmyGroup::ApplyRefit(AArmyUnit& Unit, int32 BranchIndex, const UArmyUnitDefinition& Branch)
{
	const int32 OldHealth = Unit.MaxHealth();
	const int32 OldShield = Unit.MaxShield();
	Unit.UnitIndex = BranchIndex;
	Unit.Definition = const_cast<UArmyUnitDefinition*>(&Branch);
	Unit.UnitRole = Branch.Role;
	Unit.Health = BranchPolicy::ScaleDurability(Unit.Health, OldHealth, Unit.MaxHealth());
	Unit.Shield = BranchPolicy::ScaleDurability(Unit.Shield, OldShield, Unit.MaxShield());
	// A refit is not damage: the impact cue reads the health baseline.
	Unit.LastAudioHealth = Unit.Health;
	Unit.OnRep_Appearance();
	Unit.ForceNetUpdate();
}
