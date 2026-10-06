#include "ForceOrders.h"

#include "AIController.h"
#include "ArmyGroup.h"
#include "ArmyGroupInternal.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "MapRegion.h"

bool AArmyGroup::HasArrivedAtRegion(const ACommandGameState& State, int32 RegionIndex) const
{
	const AMapRegion* Region = ForceOrderGraph::Region(State, RegionIndex);
	if (!Region)
		return false;
	// Members that stopped making progress (settled, or idle inside their grown radius) are not asked to
	// gather, so the rest decide. With none left, the region alone does.
	FVector All = FVector::ZeroVector;
	FVector Center = FVector::ZeroVector;
	FVector Offsets = FVector::ZeroVector;
	int32 Alive = 0, Joined = 0;
	float FormationRadiusSquared = 0.f;
	for (const AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		All += Unit->GetActorLocation();
		++Alive;
		if (IsUnitExempt(*Unit))
			continue;
		Center += Unit->GetActorLocation();
		const FVector Offset = FormationOffset(Unit->GetCompositionSlot());
		Offsets += Offset;
		FormationRadiusSquared = FMath::Max(FormationRadiusSquared, Offset.SizeSquared2D());
		++Joined;
	}
	if (!Alive)
		return false;
	if (!Joined)
		return Region->Contains(All / Alive);
	Center /= Joined;
	// Correct sparse-slot bias without requiring rigid slot occupancy after
	// crowd steering. A nearby mean must not stop a trailing member en route.
	if (!Region->Contains(Center)
		|| FVector::DistSquared2D(Center - Offsets / Joined, Destination) > FMath::Square(170.f))
		return false;
	const float ArrivalRadiusSquared = FMath::Square(FMath::Sqrt(FormationRadiusSquared) + 170.f);
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive() && !IsUnitExempt(*Unit)
			&& FVector::DistSquared2D(Unit->GetActorLocation(), Center) > ArrivalRadiusSquared)
			return false;
	return true;
}

const FUnitProgressSlot* AArmyGroup::FindProgress(const AArmyUnit& Unit) const
{
	const int32 Slot = Unit.GetCompositionSlot();
	return UnitProgress.IsValidIndex(Slot) && UnitProgress[Slot].Unit.Get() == &Unit ? &UnitProgress[Slot] : nullptr;
}

FUnitProgressSlot& AArmyGroup::ProgressFor(const AArmyUnit& Unit)
{
	const int32 Slot = FMath::Max(0, Unit.GetCompositionSlot());
	if (UnitProgress.Num() <= Slot)
		UnitProgress.SetNum(Slot + 1);
	FUnitProgressSlot& Entry = UnitProgress[Slot];
	if (Entry.Unit.Get() != &Unit)
		Entry = FUnitProgressSlot{ TWeakObjectPtr<const AArmyUnit>(&Unit), MovementProgressPolicy::FUnitProgress() };
	return Entry;
}

bool AArmyGroup::IsUnitSettled(const AArmyUnit& Unit) const
{
	const FUnitProgressSlot* Entry = FindProgress(Unit);
	return Entry && Entry->Progress.bTracking && Entry->Progress.bSettled;
}

float AArmyGroup::GetCloseEnoughRadius(const AArmyUnit& Unit) const
{
	const FUnitProgressSlot* Entry = FindProgress(Unit);
	return Entry && Entry->Progress.bTracking ? MovementProgressPolicy::CloseEnoughRadius(Entry->Progress.IdleSeconds) : 0.f;
}

bool AArmyGroup::IsUnitExempt(const AArmyUnit& Unit) const
{
	// A pursuing member belongs to combat, which owns its movement: it is never held out of the force.
	const FUnitProgressSlot* Entry = FindProgress(Unit);
	return Entry && !Unit.bPursuing && MovementProgressPolicy::IsExempt(Entry->Progress, Unit.GetActorLocation());
}

FVector AArmyGroup::UnitGoal(const AArmyUnit& Unit) const
{
	return Unit.bPursuing ? Unit.PursuitGoal : Destination + FormationOffset(Unit.GetCompositionSlot());
}

void AArmyGroup::ResetProgress()
{
	UnitProgress.Reset();
}

void AArmyGroup::RepathUnit(AArmyUnit& Unit, const FVector& Goal)
{
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	AAIController* AI = ArmyGroupInternal::GetReadyController(&Unit);
	ArmyGroupInternal::FPreparedMove Move;
	Move.Controller = AI;
	if (Navigation && AI
		&& ArmyGroupInternal::PrepareMove(*Navigation, Unit.GetNavAgentPropertiesRef(), AI,
			*AI->GetPathFollowingComponent(), Unit.GetNavAgentLocation(), Goal, Move, 75.f))
		ArmyGroupInternal::StartPreparedMove(Move);
}

// Samples every unit's progress toward its goal. Marching units with a goal that move less than
// MovementProgressPolicy::MinProgress per window are idle: the first escalation widens their arrival radius
// (read through IsUnitExempt), the second re-paths them once, the last settles them. Holding forces and
// units engaged in range have no goal.
void AArmyGroup::UpdateProgress()
{
	const bool bMarching = Order != EArmyOrder::Hold && Status != EForceStatus::Holding && !IsHoldingRegion();
	MovementProgressPolicy::FSample Sample;
	Sample.Now = GetWorld()->GetTimeSeconds();
	for (AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		AAIController* AI = Cast<AAIController>(Unit->GetController());
		Sample.Position = Unit->GetActorLocation();
		Sample.Goal = UnitGoal(*Unit);
		// An engaged unit has stopped on purpose; a pursuing one is judged against its pursuit goal.
		Sample.bHasGoal = bMarching && AI && (!Unit->bPursuing || AI->GetMoveStatus() != EPathFollowingStatus::Idle);
		switch (MovementProgressPolicy::Update(ProgressFor(*Unit).Progress, Sample))
		{
		case MovementProgressPolicy::EAction::Repath:
			RepathUnit(*Unit, Sample.Goal);
			break;
		case MovementProgressPolicy::EAction::Settle:
			++UnitsSettled;
			if (AI)
				AI->StopMovement();
			Unit->GetCharacterMovement()->StopMovementImmediately();
			break;
		default:
			break;
		}
	}
}

int32 AArmyGroup::GetCapacity() const
{
	if (IsValid(ProductionBuilding))
		if (const UArmyUnitDefinition* Definition = ProductionBuilding->GetProductionDefinition())
			return Definition->Capacity;
	return ForceCapacity > 0 ? ForceCapacity : 6;
}

int32 AArmyGroup::GetAliveCount() const
{
	int32 Count = 0;
	for (const AArmyUnit* Unit : Units)
		Count += IsValid(Unit) && Unit->IsAlive();
	return Count;
}

int32 AArmyGroup::GetJoinedCount() const
{
	int32 Count = 0;
	for (const AArmyUnit* Unit : Units)
		Count += IsValid(Unit) && Unit->IsAlive();
	return Count;
}

float AArmyGroup::GetBaseMarchSpeed() const
{
	float Speed = TNumericLimits<float>::Max();
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive() && Unit->GetDefinition())
			Speed = FMath::Min(Speed, Unit->GetDefinition()->MoveSpeed);
	if (Speed == TNumericLimits<float>::Max() && IsValid(ProductionBuilding))
		if (const UArmyUnitDefinition* Definition = ProductionBuilding->GetProductionDefinition())
			Speed = Definition->MoveSpeed;
	return Speed == TNumericLimits<float>::Max() ? 0.f : Speed * SpeedFactor;
}

float AArmyGroup::GetMarchSpeed() const
{
	const float Base = GetBaseMarchSpeed();
	return ForceOrders::TravelSpeed(MarchSpeed > 0.f ? FMath::Min(MarchSpeed, Base) : Base,
		Verb == EForceVerb::Retreat && Status == EForceStatus::Retreating);
}

void AArmyGroup::UpdateMarchSpeed()
{
	const float Speed = GetMarchSpeed();
	for (AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive())
			Unit->GetCharacterMovement()->MaxWalkSpeed = Speed;
}

bool AArmyGroup::CommitOrder(const FForceOrder& InOrder, bool bQueue, float SelectionSpeed)
{
	if (!HasAuthority() || !ForceOrders::CanQueue(Orders.Num(), bQueue))
		return false;
	bIdleRally = false;
	Orders.Reserve(3);
	if (!bQueue || Orders.IsEmpty())
	{
		Orders.Reset();
		Orders.Add(InOrder);
		Orders[0].SelectionSpeed = SelectionSpeed;
		MarchSpeed = SelectionSpeed;
		bWithdrawing = false;
		WithdrawalRegionIndex = INDEX_NONE;
		AppliedWaypoint = INDEX_NONE;
		Verb = InOrder.Verb;
		TargetRegionIndex = InOrder.RegionIndex;
		TargetStructure = InOrder.Structure;
		bStructureAttack = InOrder.bStructureTarget;
		Status = Verb == EForceVerb::Retreat ? EForceStatus::Retreating : EForceStatus::Marching;
		// A replacement owns movement immediately, including while navigation is rebuilding.
		// Clear the old pursuit corridor before preparing the new complete route.
		StopAllUnits();
		ResetHoldState();
		ResetProgress();
		Order = Verb == EForceVerb::Retreat ? EArmyOrder::Retreat : EArmyOrder::Move;
		AttackTarget = nullptr;
		NextWaypointAttempt = NextHoldingMaintenance = 0.f;
	}
	else
	{
		Orders.Add(InOrder);
		Orders.Last().SelectionSpeed = SelectionSpeed;
	}
	TickOrders();
	ForceNetUpdate();
	return true;
}

void AArmyGroup::RetargetIdleRally(int32 RegionIndex)
{
	if (!bIdleRally || Orders.Num() > 1)
		return;
	CommitOrder(FForceOrder(EForceVerb::MoveHold, RegionIndex), false, 0.f);
	bIdleRally = true;
}

void AArmyGroup::CompleteOrder(int32 EndRegion)
{
	const bool bCompletedRetreat = Verb == EForceVerb::Retreat;
	if (Orders.Num() > 1)
		Orders.RemoveAt(0, 1, EAllowShrinking::No);
	else
	{
		Orders.Reset();
		bIdleRally = bCompletedRetreat;
		const int32 Rally = bIdleRally && IsValid(ProductionBuilding) ? ProductionBuilding->RallyRegionIndex : EndRegion;
		Orders.Emplace(EForceVerb::MoveHold, Rally == INDEX_NONE ? EndRegion : Rally);
	}
	const FForceOrder& Next = Orders[0];
	Verb = Next.Verb;
	TargetRegionIndex = Next.RegionIndex;
	TargetStructure = Next.Structure;
	bStructureAttack = Next.bStructureTarget;
	MarchSpeed = Next.SelectionSpeed;
	NextWaypointAttempt = NextHoldingMaintenance = 0.f;
	bWithdrawing = false;
	WithdrawalRegionIndex = INDEX_NONE;
	ResetHoldState();
	ResetProgress();
	if (bIdleRally && !IsValid(ProductionBuilding))
	{
		StopAllUnits();
		Destination = GetCenter();
		Order = AppliedPhase = EArmyOrder::Attack;
	}
	// Complete at this validated waypoint without inventing a march phase,
	// including when an orphan's casualty withdrawal turns into MoveHold.
	if (Next.Verb != EForceVerb::MoveHold || Next.RegionIndex != EndRegion)
		AppliedWaypoint = INDEX_NONE;
	Status = Verb == EForceVerb::Retreat ? EForceStatus::Retreating : EForceStatus::Marching;
	ForceNetUpdate();
}
