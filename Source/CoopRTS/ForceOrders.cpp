#include "ForceOrders.h"

#include "AIController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CombatTarget.h"
#include "Commands/OrderGraph.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"

namespace
{
bool Arrived(const AArmyGroup& Force, const ACommandGameState& State, int32 RegionIndex)
{
	const AMapRegion* Region = ForceOrderGraph::Region(State, RegionIndex);
	return Force.GetJoinedCount() > 0 && Region && Region->Contains(Force.GetCenter())
		&& FVector::DistSquared2D(Force.GetCenter(), Force.Destination) <= FMath::Square(170.f);
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
		Count += IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing();
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
	return Speed == TNumericLimits<float>::Max() ? 0.f : Speed;
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
		Order = Verb == EForceVerb::Retreat ? EArmyOrder::Retreat : EArmyOrder::Move;
		AttackTarget = nullptr;
		bAutomaticFront = false;
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

bool AArmyGroup::ApplyWaypoint(int32 RegionIndex, EArmyOrder Phase, AActor* Structure)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State || !ForceOrderGraph::Region(*State, RegionIndex))
		return false;
	if (AppliedWaypoint == RegionIndex && AppliedPhase == Phase && AppliedStructure.Get() == Structure)
	{
		if (FVector::DistSquared2D(GetCenter(), Destination) <= FMath::Square(170.f))
			return true;
		for (const AArmyUnit* Unit : Units)
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
			{
				const AAIController* AI = Cast<AAIController>(Unit->GetController());
				if (Unit->bPursuing || (AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle))
					return true;
			}
	}
	FVector Anchor = State->GetRegionAnchor(RegionIndex);
	if (Structure)
	{
		FVector Direction = (GetCenter() - Structure->GetActorLocation()).GetSafeNormal2D();
		if (Direction.IsNearlyZero())
			Direction = FVector(-1.f, 0.f, 0.f);
		Anchor = Structure->GetActorLocation() + Direction * 650.f;
		Anchor.Z = State->GetRegionAnchor(RegionIndex).Z;
	}
	if (!IssueTravel(Phase, Anchor))
		return false;
	WaypointRegionIndex = AppliedWaypoint = RegionIndex;
	AppliedPhase = Phase;
	AppliedStructure = Structure;
	FrontLocation = Destination;
	FrontOrder = Verb == EForceVerb::Retreat ? EFrontOrder::FallBack
		: Status == EForceStatus::Holding    ? EFrontOrder::Defend
											 : EFrontOrder::Secure;
	bAutomaticFront = true;
	AttackTarget = Structure;
	ForceNetUpdate();
	return true;
}

void AArmyGroup::CompleteOrder(int32 EndRegion)
{
	if (Orders.Num() > 1)
		Orders.RemoveAt(0, 1, EAllowShrinking::No);
	else
	{
		Orders.Reset();
		Orders.Emplace(EForceVerb::MoveHold, EndRegion);
		Orders[0].SelectionSpeed = MarchSpeed;
	}
	const FForceOrder& Next = Orders[0];
	Verb = Next.Verb;
	TargetRegionIndex = Next.RegionIndex;
	TargetStructure = Next.Structure;
	bStructureAttack = Next.bStructureTarget;
	MarchSpeed = Next.SelectionSpeed;
	bWithdrawing = false;
	WithdrawalRegionIndex = INDEX_NONE;
	AppliedWaypoint = INDEX_NONE;
	Status = Verb == EForceVerb::Retreat ? EForceStatus::Retreating : EForceStatus::Marching;
	ForceNetUpdate();
}

void AArmyGroup::TickOrders()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	const int32 Source = ForceOrderGraph::SourceRegion(*this, *State);
	if (Source == INDEX_NONE)
		return;
	if (Orders.IsEmpty())
	{
		const int32 Rally = IsValid(ProductionBuilding) ? ProductionBuilding->RallyRegionIndex : Source;
		Orders.Reserve(3);
		Orders.Emplace(EForceVerb::MoveHold, Rally == INDEX_NONE ? Source : Rally);
		Verb = EForceVerb::MoveHold;
		TargetRegionIndex = Orders[0].RegionIndex;
		Status = EForceStatus::Marching;
	}
	uint64 Graph[ForceOrders::MaxRegions];
	const int32 Count = ForceOrderGraph::ReadGraph(*State, Graph);
	uint64 Controlled = 0, Hostiles = 0;
	for (int32 Index = 0; Index < Count; ++Index)
		if (ForceOrderGraph::Region(*State, Index) && State->GetRegionController(Index) == TeamIndex)
			Controlled |= uint64(1) << Index;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		if (It->GetTeamIndex() != TeamIndex)
			for (const AArmyUnit* Unit : It->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive())
					if (const AMapRegion* Region = State->FindRegionAt(Unit->GetActorLocation());
						Region && Region->RegionIndex >= 0 && Region->RegionIndex < Count)
						Hostiles |= uint64(1) << Region->RegionIndex;
	const int32 Home = ForceOrderGraph::TeamMain(*State, TeamIndex);
	ResumeCount = ForceOrders::ResumeCount(GetCapacity());
	const EForceStatus PreviousStatus = Status;
	if (Verb == EForceVerb::Attack && bStructureAttack
		&& !CombatTarget::IsAliveHostile(TargetStructure, TeamIndex))
	{
		CompleteOrder(Source);
		TickOrders();
		return;
	}
	if (Verb == EForceVerb::Attack && !bWithdrawing
		&& ForceOrders::ShouldWithdraw(GetAliveCount(), GetCapacity(), static_cast<uint8>(RetreatThreshold)))
	{
		bWithdrawing = true;
		AppliedWaypoint = INDEX_NONE;
		StopAllUnits();
		Order = EArmyOrder::Move;
		AttackTarget = nullptr;
		bAutomaticFront = false;
	}
	if (Verb == EForceVerb::Retreat || bWithdrawing)
	{
		const int32 Safe = ForceOrders::SafeRegion(Graph, Count, Source, Home, LastHeldRegionIndex, Controlled, Hostiles);
		if (WithdrawalRegionIndex == INDEX_NONE
			|| !(ForceOrders::ConnectedMask(Graph, Count, Home, Controlled) & ~Hostiles & (uint64(1) << WithdrawalRegionIndex)))
			WithdrawalRegionIndex = Safe;
		const bool bArrived = AppliedWaypoint == WithdrawalRegionIndex && Arrived(*this, *State, WithdrawalRegionIndex);
		Status = bArrived ? EForceStatus::Refilling : bWithdrawing ? EForceStatus::Withdrawing
																   : EForceStatus::Retreating;
		if (bArrived && bWithdrawing && ForceOrders::ShouldResume(GetJoinedCount(), GetCapacity()))
		{
			bWithdrawing = false;
			WithdrawalRegionIndex = INDEX_NONE;
			AppliedWaypoint = INDEX_NONE;
			Status = EForceStatus::Marching;
		}
		else if (bArrived && !bWithdrawing && (!IsValid(ProductionBuilding) || GetJoinedCount() >= GetCapacity()))
		{
			CompleteOrder(WithdrawalRegionIndex);
			TickOrders();
			return;
		}
		else
		{
			ApplyWaypoint(WithdrawalRegionIndex, bWithdrawing ? EArmyOrder::Move : EArmyOrder::Retreat);
			UpdateMarchSpeed();
			if (Status != PreviousStatus)
				ForceNetUpdate();
			return;
		}
	}
	const AMapRegion* Target = ForceOrderGraph::Region(*State, TargetRegionIndex);
	if (!Target)
		return;
	const bool bHostiles = (Hostiles & (uint64(1) << TargetRegionIndex)) != 0;
	const bool bTaken = State->GetRegionController(TargetRegionIndex) == TeamIndex && !bHostiles;
	const bool bCleared = (!Target->Anchor || bTaken) && !bHostiles;
	const bool bArrived = AppliedWaypoint == TargetRegionIndex && Arrived(*this, *State, TargetRegionIndex);
	if (Verb == EForceVerb::Attack && !bStructureAttack && bTaken)
	{
		CompleteOrder(Source);
		TickOrders();
		return;
	}
	if (Verb == EForceVerb::MoveHold && (bArrived || PreviousStatus == EForceStatus::Holding))
	{
		LastHeldRegionIndex = TargetRegionIndex;
		if (bCleared && Orders.Num() > 1)
		{
			CompleteOrder(TargetRegionIndex);
			TickOrders();
			return;
		}
		Status = EForceStatus::Holding;
		// Preserve today's Defend/Hold combat behavior; the hold-alarm slice owns
		// changes to responding, posts and the region leash.
		FrontOrder = EFrontOrder::Defend;
		ApplyWaypoint(TargetRegionIndex, EArmyOrder::Attack);
	}
	else
	{
		Status = EForceStatus::Marching;
		int32 Waypoint = Source;
		const AMapRegion* Current = ForceOrderGraph::Region(*State, Source);
		if (Source == TargetRegionIndex || (Current && (!Current->Anchor || (Controlled & (uint64(1) << Source))) && !(Hostiles & (uint64(1) << Source))))
			Waypoint = ForceOrders::NextWaypoint(Graph, Count, Source, TargetRegionIndex);
		if (Waypoint == TargetRegionIndex && TargetStructure)
			ApplyWaypoint(Waypoint, EArmyOrder::Attack, TargetStructure);
		else
			ApplyWaypoint(Waypoint, Verb == EForceVerb::Attack ? EArmyOrder::Attack : EArmyOrder::Move);
	}
	UpdateMarchSpeed();
	if (Status != PreviousStatus)
		ForceNetUpdate();
}
