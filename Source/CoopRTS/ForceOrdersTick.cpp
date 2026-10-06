#include "AIController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CombatTarget.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Rules/RouteCapturePolicy.h"

// Region state read once per order tick; bit N of a mask addresses region index N.
struct FForceTickContext
{
	const ACommandGameState* State = nullptr;
	int32 Source = INDEX_NONE;
	int32 Count = 0;
	int32 Home = INDEX_NONE;
	uint64 Graph[ForceOrders::MaxRegions];
	uint64 Controlled = 0;
	uint64 Hostiles = 0;
	EForceStatus PreviousStatus = EForceStatus::Marching;
	bool bTargetCompleted = false;
};

namespace
{
constexpr uint64 Bit(int32 RegionIndex)
{
	return uint64(1) << RegionIndex;
}

uint64 ControlledRegions(const ACommandGameState& State, int32 Count, int32 Team)
{
	uint64 Controlled = 0;
	for (int32 Index = 0; Index < Count; ++Index)
		if (ForceOrderGraph::Region(State, Index) && State.GetRegionController(Index) == Team)
			Controlled |= Bit(Index);
	return Controlled;
}

uint64 HostileRegions(UWorld& World, const ACommandGameState& State, int32 Count, int32 Team)
{
	uint64 Hostiles = 0;
	for (TActorIterator<AArmyGroup> It(&World); It; ++It)
		if (It->GetTeamIndex() != Team)
			for (const AArmyUnit* Unit : It->GetUnits())
				if (IsValid(Unit) && Unit->IsAlive())
					if (const AMapRegion* Region = State.FindRegionAt(Unit->GetActorLocation());
						Region && Region->RegionIndex >= 0 && Region->RegionIndex < Count)
						Hostiles |= Bit(Region->RegionIndex);
	return Hostiles;
}

// A point 650 units from the structure on the side the force approaches from.
FVector StructureStandOff(const AActor& Structure, const FVector& Center, float Z)
{
	FVector Direction = (Center - Structure.GetActorLocation()).GetSafeNormal2D();
	if (Direction.IsNearlyZero())
		Direction = FVector(-1.f, 0.f, 0.f);
	FVector Anchor = Structure.GetActorLocation() + Direction * 650.f;
	Anchor.Z = Z;
	return Anchor;
}
}

bool AArmyGroup::ShouldKeepWaypoint(const ACommandGameState& State, int32 RegionIndex, AActor* Structure) const
{
	// Reuse a settled regional waypoint using the same corrected centre.
	if (Structure ? FVector::DistSquared2D(GetCenter(), Destination) <= FMath::Square(170.f)
				  : HasArrivedAtRegion(State, RegionIndex))
		return true;
	bool bHasJoinedMember = false;
	for (const AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive())
		{
			bHasJoinedMember = true;
			// A settled unit, or one idle inside its grown radius, no longer keeps the waypoint alive.
			if (IsUnitExempt(*Unit))
				continue;
			const AAIController* AI = Cast<AAIController>(Unit->GetController());
			if (Unit->bPursuing || (AI && AI->GetMoveStatus() != EPathFollowingStatus::Idle))
				return true;
		}
	// An assembling force keeps its accepted route; recruits drive themselves.
	return !bHasJoinedMember;
}

bool AArmyGroup::IssueTravelNearAnchor(EArmyOrder Phase, const FVector& Anchor, const AMapRegion& Region)
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
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation)
		return false;
	for (const FVector2D& Direction : Directions)
	{
		const FVector Candidate = Anchor + FVector(Direction.X, Direction.Y, 0.f) * Tolerance;
		FNavLocation Projected;
		if (!Navigation->ProjectPointToNavigation(Candidate, Projected, FVector(Tolerance, Tolerance, 200.f))
			|| FVector::DistSquared2D(Anchor, Projected.Location) > FMath::Square(Tolerance)
			|| !Region.Contains(Projected.Location))
			continue;
		if (IssueTravel(Phase, Projected.Location))
			return true;
	}
	return false;
}

bool AArmyGroup::ApplyWaypoint(int32 RegionIndex, EArmyOrder Phase, AActor* Structure)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const AMapRegion* Region = State ? ForceOrderGraph::Region(*State, RegionIndex) : nullptr;
	if (!Region)
		return false;
	if (AppliedWaypoint == RegionIndex && AppliedPhase == Phase && AppliedStructure.Get() == Structure
		&& ShouldKeepWaypoint(*State, RegionIndex, Structure))
		return true;
	// Blocked formations retry at the old front-maintenance cadence, not five
	// complete synchronous slot-path solves every combat tick.
	const float Now = GetWorld()->GetTimeSeconds();
	if (Now < NextWaypointAttempt)
		return false;
	NextWaypointAttempt = Now + 2.f;
	FVector Anchor = State->GetRegionAnchor(RegionIndex);
	if (Structure)
		Anchor = StructureStandOff(*Structure, GetCenter(), Anchor.Z);
	if (!IssueTravel(Phase, Anchor) && (Structure || !IssueTravelNearAnchor(Phase, Anchor, *Region)))
		return false;
	WaypointRegionIndex = AppliedWaypoint = RegionIndex;
	AppliedPhase = Phase;
	AppliedStructure = Structure;
	NextWaypointAttempt = 0.f;
	AttackTarget = Structure;
	ForceNetUpdate();
	return true;
}

void AArmyGroup::AdvanceOrder(int32 EndRegion)
{
	CompleteOrder(EndRegion);
	TickOrders();
}

void AArmyGroup::EnsureActiveOrder(int32 Source)
{
	if (!Orders.IsEmpty())
		return;
	const int32 Rally = IsValid(ProductionBuilding) ? ProductionBuilding->RallyRegionIndex : Source;
	bIdleRally = true;
	MarchSpeed = 0.f;
	Orders.Reserve(3);
	Orders.Emplace(EForceVerb::MoveHold, Rally == INDEX_NONE ? Source : Rally);
	Verb = EForceVerb::MoveHold;
	TargetRegionIndex = Orders[0].RegionIndex;
	Status = EForceStatus::Marching;
}

void AArmyGroup::CancelOrphanRally(int32 Source)
{
	if (!bIdleRally || IsValid(ProductionBuilding)
		|| (Status == EForceStatus::Holding && !IsHoldingRegion()))
		return;
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

void AArmyGroup::ReadTickContext(FForceTickContext& Ctx)
{
	const ACommandGameState& State = *Ctx.State;
	Ctx.Count = ForceOrderGraph::ReadGraph(State, Ctx.Graph);
	Ctx.Controlled = ControlledRegions(State, Ctx.Count, TeamIndex);
	Ctx.Hostiles = HostileRegions(*GetWorld(), State, Ctx.Count, TeamIndex);
	Ctx.Home = ForceOrderGraph::TeamMain(State, TeamIndex);
	ResumeCount = ForceOrders::ResumeCount(GetCapacity());
	Ctx.PreviousStatus = Status;
	Ctx.bTargetCompleted = Verb == EForceVerb::Attack
		&& (bStructureAttack ? !CombatTarget::IsAliveHostile(TargetStructure, TeamIndex)
							 : State.GetRegionController(TargetRegionIndex) == TeamIndex && !(Ctx.Hostiles & Bit(TargetRegionIndex)));
}

void AArmyGroup::FinishTick(const FForceTickContext& Ctx)
{
	UpdateMarchSpeed();
	UpdateIntentRoutes(Ctx.Graph, Ctx.Count, Ctx.Source, Ctx.Controlled, Ctx.Hostiles, Ctx.bTargetCompleted);
	if (Status != Ctx.PreviousStatus)
		ForceNetUpdate();
}

bool AArmyGroup::TickWithdrawal(const FForceTickContext& Ctx)
{
	const int32 Safe = ForceOrders::SafeRegion(Ctx.Graph, Ctx.Count, Ctx.Source, Ctx.Home, LastHeldRegionIndex, Ctx.Controlled, Ctx.Hostiles);
	if (WithdrawalRegionIndex == INDEX_NONE
		|| !(ForceOrders::ConnectedMask(Ctx.Graph, Ctx.Count, Ctx.Home, Ctx.Controlled) & ~Ctx.Hostiles & Bit(WithdrawalRegionIndex)))
		WithdrawalRegionIndex = Safe;
	const bool bArrived = AppliedWaypoint == WithdrawalRegionIndex
		&& (Ctx.PreviousStatus == EForceStatus::Refilling || HasArrivedAtRegion(*Ctx.State, WithdrawalRegionIndex));
	Status = bArrived ? EForceStatus::Refilling : bWithdrawing ? EForceStatus::Withdrawing
															   : EForceStatus::Retreating;
	// Target loss never aborts a casualty withdrawal in hostile ground.
	// Orphans cannot refill: finish at safety instead of waiting forever at 2/6.
	if (bArrived && bWithdrawing && (!IsValid(ProductionBuilding) || Ctx.bTargetCompleted))
	{
		AdvanceOrder(WithdrawalRegionIndex);
		return true;
	}
	if (bArrived && bWithdrawing && ForceOrders::ShouldResume(GetJoinedCount(), GetCapacity()))
	{
		bWithdrawing = false;
		WithdrawalRegionIndex = INDEX_NONE;
		AppliedWaypoint = INDEX_NONE;
		NextWaypointAttempt = 0.f;
		Status = EForceStatus::Marching;
		return false;
	}
	if (bArrived && !bWithdrawing && (!IsValid(ProductionBuilding) || GetJoinedCount() >= GetCapacity()))
	{
		AdvanceOrder(WithdrawalRegionIndex);
		return true;
	}
	ApplyWaypoint(WithdrawalRegionIndex, bArrived ? EArmyOrder::Attack : bWithdrawing ? EArmyOrder::Move
																					  : EArmyOrder::Retreat);
	FinishTick(Ctx);
	return true;
}

void AArmyGroup::MaintainHoldWaypoint(const FForceTickContext& Ctx, const AMapRegion& Target)
{
	// Once assigned, posts/alarms own Destination. Never pull a holder back
	// to the anchor; an unowned capture target must be secured before posts.
	if ((bIdleRally && !IsValid(ProductionBuilding)) || IsHoldingRegion()
		|| (Ctx.PreviousStatus == EForceStatus::Holding && GetWorld()->GetTimeSeconds() < NextHoldingMaintenance))
		return;
	if (ApplyWaypoint(TargetRegionIndex, EArmyOrder::Attack)
		&& (!Target.Anchor || Ctx.State->GetRegionController(TargetRegionIndex) == TeamIndex)
		&& !Target.GetDefendPosts().IsEmpty())
	{
		HoldRegionIndex = TargetRegionIndex;
		ForceNetUpdate();
	}
	NextHoldingMaintenance = GetWorld()->GetTimeSeconds() + 2.f;
}

bool AArmyGroup::TickHold(const FForceTickContext& Ctx, const AMapRegion& Target, bool bCleared)
{
	LastHeldRegionIndex = TargetRegionIndex;
	if (bCleared && Orders.Num() > 1)
	{
		AdvanceOrder(TargetRegionIndex);
		return false;
	}
	Status = EForceStatus::Holding;
	MarchSpeed = Orders[0].SelectionSpeed = 0.f;
	MaintainHoldWaypoint(Ctx, Target);
	return true;
}

void AArmyGroup::TickMarch(const FForceTickContext& Ctx)
{
	Status = EForceStatus::Marching;
	const int32 Source = Ctx.Source;
	const AMapRegion* Current = ForceOrderGraph::Region(*Ctx.State, Source);
	if (!Current)
		return;
	// Do not chase off the route to unlock an intermediate waypoint. Capture
	// uncontested ground on the route; pass a contested anchor only after physical
	// arrival. Ground the path merely crosses is not captured.
	RouteCapturePolicy::FMarch March;
	March.Source = Source;
	March.Target = TargetRegionIndex;
	March.Origin = Orders[0].RouteOrigin;
	March.Applied = AppliedWaypoint;
	March.bHasAnchor = Current->Anchor != nullptr;
	March.bControlled = (Ctx.Controlled & Bit(Source)) != 0;
	March.bContested = March.bHasAnchor
		&& (TeamIndex == 0 ? Current->Anchor->bEnemyPresent : Current->Anchor->bFriendlyPresent);
	March.bArrived = March.bContested && AppliedWaypoint == Source && HasArrivedAtRegion(*Ctx.State, Source);
	const RouteCapturePolicy::FDecision Decision = RouteCapturePolicy::Decide(Ctx.Graph, Ctx.Count, March);
	Orders[0].RouteOrigin = Decision.Origin;
	int32 Waypoint = Source;
	if (Decision.Step == RouteCapturePolicy::EStep::Advance)
		Waypoint = ForceOrders::NextWaypoint(Ctx.Graph, Ctx.Count, Source, TargetRegionIndex);
	else if (Decision.Step == RouteCapturePolicy::EStep::Continue)
		Waypoint = AppliedWaypoint;
	if (Waypoint == TargetRegionIndex && TargetStructure)
		ApplyWaypoint(Waypoint, EArmyOrder::Attack, TargetStructure);
	else
		ApplyWaypoint(Waypoint, Verb == EForceVerb::Attack ? EArmyOrder::Attack : EArmyOrder::Move);
}

// Returns whether the tick should finish by publishing speed, routes and status.
bool AArmyGroup::TickTarget(const FForceTickContext& Ctx)
{
	const ACommandGameState& State = *Ctx.State;
	const AMapRegion* Target = ForceOrderGraph::Region(State, TargetRegionIndex);
	if (!Target)
		return false;
	const bool bHostiles = (Ctx.Hostiles & Bit(TargetRegionIndex)) != 0;
	const bool bTaken = State.GetRegionController(TargetRegionIndex) == TeamIndex && !bHostiles;
	const bool bCleared = (!Target->Anchor || bTaken) && !bHostiles;
	const bool bArrived = AppliedWaypoint == TargetRegionIndex && HasArrivedAtRegion(State, TargetRegionIndex);
	if (Verb == EForceVerb::Attack && !bStructureAttack && bTaken && bArrived)
	{
		AdvanceOrder(TargetRegionIndex);
		return false;
	}
	if (Verb == EForceVerb::MoveHold && (bArrived || Ctx.PreviousStatus == EForceStatus::Holding))
		return TickHold(Ctx, *Target, bCleared);
	TickMarch(Ctx);
	return true;
}

void AArmyGroup::TickOrders()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || !State || State->MatchResult != EMatchResult::Ongoing)
		return;
	const int32 Source = ForceOrderGraph::SourceRegion(*this, *State);
	if (Source == INDEX_NONE)
		return;
	EnsureActiveOrder(Source);
	UpdateProgress();
	CancelOrphanRally(Source);
	FForceTickContext Ctx;
	Ctx.State = State;
	Ctx.Source = Source;
	ReadTickContext(Ctx);
	if (bStructureAttack && Ctx.bTargetCompleted && !bWithdrawing)
	{
		AdvanceOrder(Source);
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
	if ((Verb == EForceVerb::Retreat || bWithdrawing) && TickWithdrawal(Ctx))
		return;
	if (TickTarget(Ctx))
		FinishTick(Ctx);
}
