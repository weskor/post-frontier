#include "ForceGoals.h"

#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Content/UnitDefinition.h"
#include "Engine/World.h"
#include "NavigationSystem.h"
#include "MapRegion.h"
#include "Commands/GoalGraph.h"

namespace
{
using namespace CommandGoalGraph;

void BuildPath(FForceGoalDriver& Driver, int32 Source, int32 Target)
{
	Driver.PathLength = Driver.PathCursor = 0;
	int32 Current = Source;
	while (Current != INDEX_NONE && Driver.PathLength < ForceGoals::MaxRegions)
	{
		Driver.Path[Driver.PathLength++] = Current;
		if (Current == Target)
			return;
		Current = ForceGoals::NextWaypoint(Driver.Graph, Driver.RegionCount, Current, Target);
	}
	Driver.PathLength = 0;
}
}

void ACommandBuilding::CommitGoal(EForceGoal Goal, int32 RegionIndex, int32 Source, const uint64* Graph, int32 Count)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	ForceGoal = Goal;
	GoalRegionIndex = RegionIndex;
	GoalDriver.bEnabled = true;
	GoalDriver.bInitialized = true;
	GoalDriver.bPathDirty = true;
	GoalDriver.bRefilling = false;
	GoalDriver.AppliedWaypoint = INDEX_NONE;
	GoalDriver.AppliedOrder = 255;
	GoalDriver.LastHeld = State->GetRegionController(Source) == TeamIndex ? Source : INDEX_NONE;
	if (GoalDriver.LastHeld == INDEX_NONE)
	{
		const AMapRegion* Home = State->FindRegionAt(GetActorLocation());
		if (Home && State->GetRegionController(Home->RegionIndex) == TeamIndex)
			GoalDriver.LastHeld = Home->RegionIndex;
	}
	GoalDriver.RegionCount = Count;
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
		GoalDriver.Graph[Index] = Graph[Index];
	TickGoal();
	ForceNetUpdate();
}

bool ACommandBuilding::ApplyRegionFront(EFrontOrder Order, const AMapRegion& Region)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	const FVector Anchor = State->GetRegionAnchor(Region.RegionIndex);
	if (ApplyFront(Order, Anchor))
		return true;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation)
		return false;
	// A structure can obstruct a formation slot even when the anchor is navigable.
	// Region goals may move the centre within the existing anchor tolerance;
	// precise point orders still reject obstructed formations atomically.
	constexpr float Tolerance = 75.f;
	constexpr float Diagonal = UE_INV_SQRT_2;
	static const FVector2D Directions[] = {
		{ 1.f, 0.f }, { -1.f, 0.f }, { 0.f, 1.f }, { 0.f, -1.f },
		{ Diagonal, Diagonal }, { -Diagonal, Diagonal }, { Diagonal, -Diagonal }, { -Diagonal, -Diagonal }
	};
	for (const FVector2D& Direction : Directions)
	{
		const FVector Candidate = Anchor + FVector(Direction.X, Direction.Y, 0.f) * Tolerance;
		FNavLocation Projected;
		if (!Navigation->ProjectPointToNavigation(Candidate, Projected, FVector(Tolerance, Tolerance, 200.f))
			|| FVector::DistSquared2D(Anchor, Projected.Location) > FMath::Square(Tolerance)
			|| !Region.Contains(Projected.Location))
			continue;
		if (ApplyFront(Order, Projected.Location))
			return true;
	}
	return false;
}

void ACommandBuilding::TickGoal()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsAlive() || IsActorBeingDestroyed() || !IsProducer())
		return;
	if (!GoalDriver.bInitialized)
	{
		const AMapRegion* Home = State->FindRegionAt(GetActorLocation());
		if (!Home)
			return; // Level discovery can precede deferred producer spawning.
		GoalRegionIndex = Home->RegionIndex;
		GoalDriver.LastHeld = Home->RegionIndex;
		GoalDriver.RegionCount = ReadGraph(*State, GoalDriver.Graph);
		GoalDriver.bInitialized = true;
		GoalDriver.bEnabled = TeamIndex != 5; // Enemy planner still uses its internal location fronts.
		ForceNetUpdate();
	}
	if (!GoalDriver.bEnabled || !IsComplete() || !bForceConfigured || !IsValid(ForceGroup)
		|| ForceGroup->IsActorBeingDestroyed())
		return;

	const bool bAdvancing = ForceGoal == EForceGoal::Expand || ForceGoal == EForceGoal::Assault;
	if (bAdvancing)
		for (int32 Index = 0; Index < GoalDriver.RegionCount; ++Index)
		{
			const int32 Controller = State->GetRegionController(Index);
			if (GoalDriver.Controllers[Index] != Controller)
			{
				GoalDriver.Controllers[Index] = Controller;
				GoalDriver.bPathDirty = true;
			}
		}
	int32 Joined = 0, Travelling = 0;
	if (bAdvancing)
		GetForceCounts(Joined, Travelling);
	if (ForceGoal == EForceGoal::Assault)
	{
		const UArmyUnitDefinition* Definition = GetProductionDefinition();
		const int32 Capacity = Definition ? GetForceCapacity(*Definition) : 0;
		const int32 Alive = Joined + Travelling;
		if (!GoalDriver.bRefilling && Capacity > 0 && Alive * 5 < Capacity * 2)
		{
			GoalDriver.bRefilling = true;
			// Prefer the nearest still-controlled region already visited on this route.
			for (int32 Index = FMath::Min(GoalDriver.PathCursor - 1, GoalDriver.PathLength - 1); Index >= 0; --Index)
				if (State->GetRegionController(GoalDriver.Path[Index]) == TeamIndex)
				{
					GoalDriver.LastHeld = GoalDriver.Path[Index];
					break;
				}
		}
		else if (GoalDriver.bRefilling && Capacity > 0 && Alive >= Capacity)
		{
			GoalDriver.bRefilling = false;
			for (int32 Index = 0; Index < GoalDriver.PathLength; ++Index)
				if (GoalDriver.Path[Index] == GoalDriver.LastHeld)
				{
					GoalDriver.PathCursor = Index;
					break;
				}
		}
		if (GoalDriver.bRefilling && State->GetRegionController(GoalDriver.LastHeld) != TeamIndex)
		{
			const AMapRegion* Home = State->FindRegionAt(GetActorLocation());
			GoalDriver.LastHeld = Home && State->GetRegionController(Home->RegionIndex) == TeamIndex
				? Home->RegionIndex
				: INDEX_NONE;
			for (int32 Index = GoalDriver.PathCursor - 1; Index >= 0; --Index)
				if (State->GetRegionController(GoalDriver.Path[Index]) == TeamIndex)
				{
					GoalDriver.LastHeld = GoalDriver.Path[Index];
					break;
				}
		}
	}

	int32 Waypoint = GoalRegionIndex;
	EFrontOrder Order = EFrontOrder::Defend;
	if (ForceGoal == EForceGoal::FallBack)
	{
		Order = EFrontOrder::FallBack;
	}
	else if (ForceGoal == EForceGoal::Assault && GoalDriver.bRefilling)
	{
		Waypoint = GoalDriver.LastHeld;
	}
	else if (ForceGoal == EForceGoal::Expand || ForceGoal == EForceGoal::Assault)
	{
		if (GoalDriver.bPathDirty)
		{
			BuildPath(GoalDriver, SourceRegion(*this, *State), GoalRegionIndex);
			GoalDriver.bPathDirty = false;
		}
		if (GoalDriver.PathLength == 0)
			return;
		Waypoint = GoalDriver.Path[GoalDriver.PathCursor];
		const AMapRegion* Current = Region(*State, Waypoint);
		const bool bReached = Joined > 0 && Current && Current->Contains(ForceGroup->GetCenter());
		if (bReached && State->GetRegionController(Waypoint) == TeamIndex
			&& !State->IsRegionContested(Waypoint, TeamIndex))
		{
			GoalDriver.LastHeld = Waypoint;
			if (GoalDriver.PathCursor + 1 < GoalDriver.PathLength)
				Waypoint = GoalDriver.Path[++GoalDriver.PathCursor];
			else if (ForceGoal == EForceGoal::Expand)
			{
				ForceGoal = EForceGoal::Hold;
				ForceNetUpdate();
			}
		}
		if (ForceGoal != EForceGoal::Hold)
			Order = EFrontOrder::Secure;
	}
	const AMapRegion* WaypointRegion = Region(*State, Waypoint);
	if (!WaypointRegion)
		return;
	GoalDriver.Waypoint = Waypoint;
	// Wipes preserve this cache and the persistent group; replacement units inherit the front.
	if (GoalDriver.AppliedWaypoint == Waypoint && GoalDriver.AppliedOrder == static_cast<uint8>(Order))
		return;
	const bool bAccepted = ForceGoal == EForceGoal::FallBack
		? ApplyFront(Order, ForceGroup->GetHomeLocation())
		: ApplyRegionFront(Order, *WaypointRegion);
	if (!bAccepted)
		return;
	GoalDriver.bEnabled = true; // Internal front application must not disable the goal driver.
	GoalDriver.AppliedWaypoint = Waypoint;
	GoalDriver.AppliedOrder = static_cast<uint8>(Order);
}
