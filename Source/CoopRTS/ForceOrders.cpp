#include "ForceOrders.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MapRegion.h"

bool AArmyGroup::HasArrivedAtRegion(const ACommandGameState& State, int32 RegionIndex) const
{
	const AMapRegion* Region = ForceOrderGraph::Region(State, RegionIndex);
	if (!Region)
		return false;
	FVector Center = FVector::ZeroVector;
	FVector Offsets = FVector::ZeroVector;
	int32 Joined = 0;
	float FormationRadiusSquared = 0.f;
	for (const AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		Center += Unit->GetActorLocation();
		const FVector Offset = FormationOffset(Unit->GetCompositionSlot());
		Offsets += Offset;
		FormationRadiusSquared = FMath::Max(FormationRadiusSquared, Offset.SizeSquared2D());
		++Joined;
	}
	if (!Joined)
		return false;
	Center /= Joined;
	// Correct sparse-slot bias without requiring rigid slot occupancy after
	// crowd steering. A nearby mean must not stop a trailing member en route.
	if (!Region->Contains(Center)
		|| FVector::DistSquared2D(Center - Offsets / Joined, Destination) > FMath::Square(170.f))
		return false;
	const float ArrivalRadiusSquared = FMath::Square(FMath::Sqrt(FormationRadiusSquared) + 170.f);
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive()
			&& FVector::DistSquared2D(Unit->GetActorLocation(), Center) > ArrivalRadiusSquared)
			return false;
	return true;
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
