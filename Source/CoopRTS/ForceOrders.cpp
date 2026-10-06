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

// The members that decide arrival, each with the fitted slot it is judged against.
struct FArrivalLayout
{
	TArray<MovementProgressPolicy::FArrivalMember, TInlineAllocator<6>> Members;
	MovementProgressPolicy::FArrival Arrival;
	// Mean of every living member, for when none is left to judge.
	FVector AllCenter = FVector::ZeroVector;
	// Every living member is left out.
	bool bAllLeftOut = false;
};

// The members that still speak for the force, each against its fitted slot. Members left out are those settled
// by progress, or with bSkipExempt those idle inside their grown radius too (never a pursuing member).
// False when no member lives.
bool AArmyGroup::GatherArrival(const ACommandGameState& State, bool bSkipExempt, FArrivalLayout& Out) const
{
	FFittedSlots Fitted;
	FitSlots(&State, Fitted);
	int32 Alive = 0;
	FVector SlotCentroid = FVector::ZeroVector;
	for (const AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		Out.AllCenter += Unit->GetActorLocation();
		SlotCentroid += PlannedSlot(*Unit, Fitted);
		++Alive;
		if (bSkipExempt ? IsUnitExempt(*Unit) : !Unit->bPursuing && IsUnitSettled(*Unit))
			continue;
		const AAIController* AI = Cast<AAIController>(Unit->GetController());
		Out.Members.Add({ Unit->GetActorLocation(), PlannedSlot(*Unit, Fitted),
			!Unit->bPursuing && AI && AI->GetMoveStatus() == EPathFollowingStatus::Idle });
	}
	if (!Alive)
		return false;
	Out.AllCenter /= Alive;
	SlotCentroid /= Alive;
	// The planned targets are the members' own slots (a column's centroid is not the box centre): the radius is
	// measured from their centroid.
	float FittedRadius = 0.f;
	for (const MovementProgressPolicy::FArrivalMember& Member : Out.Members)
		FittedRadius = FMath::Max(FittedRadius, static_cast<float>(FVector::Dist2D(Member.Slot, SlotCentroid)));
	Out.bAllLeftOut = Out.Members.IsEmpty();
	Out.Arrival = MovementProgressPolicy::JudgeArrival(Out.Members, FittedRadius);
	return true;
}

// Where the force is for the executor: the mean of its members, without those settled by progress (unless every
// member is), so one wedged unit does not drag the force's region behind it. GetCenter stays the mean of all.
FVector AArmyGroup::GetMarchCenter() const
{
	FVector All = FVector::ZeroVector, Free = FVector::ZeroVector;
	int32 AllCount = 0, FreeCount = 0;
	for (const AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		All += Unit->GetActorLocation();
		++AllCount;
		if (Unit->bPursuing || !IsUnitSettled(*Unit))
		{
			Free += Unit->GetActorLocation();
			++FreeCount;
		}
	}
	return FreeCount > 0 ? Free / FreeCount : AllCount > 0 ? All / AllCount
														   : GetCenter();
}

int32 AArmyGroup::MarchSourceRegion(const ACommandGameState& State) const
{
	if (GetJoinedCount() == 0)
		return ForceOrderGraph::SourceRegion(*this, State);
	const AMapRegion* Region = State.FindRegionAt(GetMarchCenter());
	return Region ? Region->RegionIndex : INDEX_NONE;
}

bool AArmyGroup::HasArrivedAtRegion(const ACommandGameState& State, int32 RegionIndex) const
{
	const AMapRegion* Region = ForceOrderGraph::Region(State, RegionIndex);
	FArrivalLayout Layout;
	// Members that stopped making progress are not asked to gather; with none left the region alone decides.
	if (!Region || !GatherArrival(State, true, Layout))
		return false;
	if (Layout.bAllLeftOut)
		return Region->Contains(Layout.AllCenter);
	return Region->Contains(Layout.Arrival.Center) && Layout.Arrival.bGathered;
}

// Whether the force stands at the stand-off point of a structure order, by the same fitted mean.
bool AArmyGroup::HasReachedStandOff() const
{
	FArrivalLayout Layout;
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State || !GatherArrival(*State, false, Layout))
		return false;
	const MovementProgressPolicy::FArrival& Arrival = Layout.Arrival;
	return Layout.bAllLeftOut ? FVector::Dist2D(Layout.AllCenter, Destination) <= MovementProgressPolicy::ArrivalTolerance
							  : FVector::Dist2D(Arrival.Center, Arrival.ExpectedMean) <= MovementProgressPolicy::ArrivalTolerance;
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

FVector AArmyGroup::UnitGoal(const AArmyUnit& Unit, const FFittedSlots& Fitted) const
{
	return Unit.bPursuing ? Unit.PursuitGoal : PlannedSlot(Unit, Fitted);
}

FVector AArmyGroup::PlannedSlot(const AArmyUnit& Unit, const FFittedSlots& Fitted) const
{
	if (!Unit.FormationTarget.IsZero())
		return Unit.FormationTarget;
	const int32 Index = Unit.GetCompositionSlot();
	return Fitted.Goals.IsValidIndex(Index) ? Fitted.Goals[Index] : Destination + FormationOffset(Index);
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
	FFittedSlots Fitted;
	FitSlots(GetWorld()->GetGameState<ACommandGameState>(), Fitted);
	for (AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		AAIController* AI = Cast<AAIController>(Unit->GetController());
		Sample.Position = Unit->GetActorLocation();
		Sample.Goal = UnitGoal(*Unit, Fitted);
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
