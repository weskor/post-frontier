#include "ForceOrders.h"

#include "AIController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CombatTarget.h"
#include "Commands/OrderGraph.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

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
		if (!IsValid(Unit) || !Unit->IsAlive() || Unit->IsReinforcing())
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
		if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing()
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

bool AArmyGroup::ApplyWaypoint(int32 RegionIndex, EArmyOrder Phase, AActor* Structure)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State || !ForceOrderGraph::Region(*State, RegionIndex))
		return false;
	if (AppliedWaypoint == RegionIndex && AppliedPhase == Phase && AppliedStructure.Get() == Structure)
	{
		// Reuse a settled regional waypoint using the same corrected centre.
		if (Structure ? FVector::DistSquared2D(GetCenter(), Destination) <= FMath::Square(170.f)
					  : HasArrivedAtRegion(*State, RegionIndex))
			return true;
		bool bHasJoinedMember = false;
		for (const AArmyUnit* Unit : Units)
			if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
			{
				bHasJoinedMember = true;
				const AAIController* AI = Cast<AAIController>(Unit->GetController());
				if (Unit->bPursuing || (AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle))
					return true;
			}
		// An assembling force keeps its accepted route; recruits drive themselves.
		if (!bHasJoinedMember)
			return true;
	}
	// Blocked formations retry at the old front-maintenance cadence, not five
	// complete synchronous slot-path solves every combat tick.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextWaypointAttempt)
		return false;
	NextWaypointAttempt = Now + 2.f;
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
	{
		// A building nav cutout can obstruct an exact formation slot. Region
		// intent may shift the centre, but its actual projection stays within
		// the anchor tolerance and every member still requires a complete path.
		constexpr float Tolerance = 75.f;
		constexpr float Diagonal = UE_INV_SQRT_2;
		static const FVector2D Directions[] = {
			{ 1.f, 0.f }, { -1.f, 0.f }, { 0.f, 1.f }, { 0.f, -1.f },
			{ Diagonal, Diagonal }, { -Diagonal, Diagonal }, { Diagonal, -Diagonal }, { -Diagonal, -Diagonal }
		};
		bool bAccepted = false;
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
		const AMapRegion* Region = ForceOrderGraph::Region(*State, RegionIndex);
		if (!Structure && Navigation)
			for (const FVector2D& Direction : Directions)
			{
				const FVector Candidate = Anchor + FVector(Direction.X, Direction.Y, 0.f) * Tolerance;
				FNavLocation Projected;
				if (!Navigation->ProjectPointToNavigation(Candidate, Projected, FVector(Tolerance, Tolerance, 200.f))
					|| FVector::DistSquared2D(Anchor, Projected.Location) > FMath::Square(Tolerance)
					|| !Region->Contains(Projected.Location))
					continue;
				if (IssueTravel(Phase, Projected.Location))
				{
					bAccepted = true;
					break;
				}
			}
		if (!bAccepted)
			return false;
	}
	WaypointRegionIndex = AppliedWaypoint = RegionIndex;
	AppliedPhase = Phase;
	AppliedStructure = Structure;
	NextWaypointAttempt = 0.f;
	AttackTarget = Structure;
	ForceNetUpdate();
	return true;
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
		bIdleRally = true;
		MarchSpeed = 0.f;
		Orders.Reserve(3);
		Orders.Emplace(EForceVerb::MoveHold, Rally == INDEX_NONE ? Source : Rally);
		Verb = EForceVerb::MoveHold;
		TargetRegionIndex = Orders[0].RegionIndex;
		Status = EForceStatus::Marching;
	}
	if (bIdleRally && !IsValid(ProductionBuilding)
		&& (Status != EForceStatus::Holding || IsHoldingRegion()))
	{
		// Losing the producer cancels only implicit rally travel, not explicit orders.
		StopAllUnits();
		ResetHoldState();
		Destination = GetCenter();
		TargetRegionIndex = Orders[0].RegionIndex = Source;
		MarchSpeed = Orders[0].SelectionSpeed = 0.f;
		Order = AppliedPhase = EArmyOrder::Attack;
		WaypointRegionIndex = AppliedWaypoint = Source;
		Status = EForceStatus::Holding;
		ForceNetUpdate();
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
	const bool bTargetCompleted = Verb == EForceVerb::Attack && (bStructureAttack ? !CombatTarget::IsAliveHostile(TargetStructure, TeamIndex) : State->GetRegionController(TargetRegionIndex) == TeamIndex && !(Hostiles & (uint64(1) << TargetRegionIndex)));
	if (bStructureAttack && bTargetCompleted && !bWithdrawing)
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
		NextWaypointAttempt = 0.f;
	}
	if (Verb == EForceVerb::Retreat || bWithdrawing)
	{
		const int32 Safe = ForceOrders::SafeRegion(Graph, Count, Source, Home, LastHeldRegionIndex, Controlled, Hostiles);
		if (WithdrawalRegionIndex == INDEX_NONE
			|| !(ForceOrders::ConnectedMask(Graph, Count, Home, Controlled) & ~Hostiles & (uint64(1) << WithdrawalRegionIndex)))
			WithdrawalRegionIndex = Safe;
		const bool bArrived = AppliedWaypoint == WithdrawalRegionIndex
			&& (PreviousStatus == EForceStatus::Refilling || HasArrivedAtRegion(*State, WithdrawalRegionIndex));
		Status = bArrived ? EForceStatus::Refilling : bWithdrawing ? EForceStatus::Withdrawing
																   : EForceStatus::Retreating;
		// Target loss never aborts a casualty withdrawal in hostile ground.
		// Orphans cannot refill: finish at safety instead of waiting forever at 2/6.
		if (bArrived && bWithdrawing && (!IsValid(ProductionBuilding) || bTargetCompleted))
		{
			CompleteOrder(WithdrawalRegionIndex);
			TickOrders();
			return;
		}
		if (bArrived && bWithdrawing && ForceOrders::ShouldResume(GetJoinedCount(), GetCapacity()))
		{
			bWithdrawing = false;
			WithdrawalRegionIndex = INDEX_NONE;
			AppliedWaypoint = INDEX_NONE;
			NextWaypointAttempt = 0.f;
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
			ApplyWaypoint(WithdrawalRegionIndex, bArrived ? EArmyOrder::Attack : bWithdrawing ? EArmyOrder::Move
																							  : EArmyOrder::Retreat);
			UpdateMarchSpeed();
			UpdateIntentRoutes(Graph, Count, Source, Controlled, Hostiles);
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
	const bool bArrived = AppliedWaypoint == TargetRegionIndex && HasArrivedAtRegion(*State, TargetRegionIndex);
	if (Verb == EForceVerb::Attack && !bStructureAttack && bTaken && bArrived)
	{
		CompleteOrder(TargetRegionIndex);
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
		MarchSpeed = Orders[0].SelectionSpeed = 0.f;
		// Once assigned, posts/alarms own Destination. Never pull a holder back
		// to the anchor; an unowned capture target must be secured before posts.
		if (!(bIdleRally && !IsValid(ProductionBuilding)) && !IsHoldingRegion()
			&& (PreviousStatus != EForceStatus::Holding || GetWorld()->GetTimeSeconds() >= NextHoldingMaintenance))
		{
			if (ApplyWaypoint(TargetRegionIndex, EArmyOrder::Attack)
				&& (!Target->Anchor || State->GetRegionController(TargetRegionIndex) == TeamIndex)
				&& !Target->GetDefendPosts().IsEmpty())
			{
				HoldRegionIndex = TargetRegionIndex;
				ForceNetUpdate();
			}
			NextHoldingMaintenance = GetWorld()->GetTimeSeconds() + 2.f;
		}
	}
	else
	{
		Status = EForceStatus::Marching;
		int32 Waypoint = Source;
		const AMapRegion* Current = ForceOrderGraph::Region(*State, Source);
		// Do not chase off the route to unlock an intermediate waypoint. Capture
		// uncontested ground; pass a contested anchor only after physical arrival.
		const bool bContested = Current && Current->Anchor
			&& (TeamIndex == 0 ? Current->Anchor->bEnemyPresent : Current->Anchor->bFriendlyPresent);
		if (Source == TargetRegionIndex || (Current && (!Current->Anchor || (Controlled & (uint64(1) << Source))))
			|| (bContested && AppliedWaypoint != INDEX_NONE
				&& (AppliedWaypoint != Source || HasArrivedAtRegion(*State, Source))))
			Waypoint = ForceOrders::NextWaypoint(Graph, Count, Source, TargetRegionIndex);
		if (Waypoint == TargetRegionIndex && TargetStructure)
			ApplyWaypoint(Waypoint, EArmyOrder::Attack, TargetStructure);
		else
			ApplyWaypoint(Waypoint, Verb == EForceVerb::Attack ? EArmyOrder::Attack : EArmyOrder::Move);
	}
	UpdateMarchSpeed();
	UpdateIntentRoutes(Graph, Count, Source, Controlled, Hostiles);
	if (Status != PreviousStatus)
		ForceNetUpdate();
}
