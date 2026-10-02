#include "ForceGoals.h"

#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Content/UnitDefinition.h"
#include "Engine/World.h"
#include "MapRegion.h"

namespace
{
	const AMapRegion* Region(const ACommandGameState& State, int32 Index)
	{
		for (const AMapRegion* Candidate : State.Regions)
			if (IsValid(Candidate) && Candidate->RegionIndex == Index) return Candidate;
		return nullptr;
	}

	int32 ReadGraph(const ACommandGameState& State, uint64* Graph)
	{
		for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index) Graph[Index] = 0;
		int32 Count = 0;
		for (const AMapRegion* Item : State.Regions)
		{
			if (!IsValid(Item) || Item->RegionIndex < 0 || Item->RegionIndex >= ForceGoals::MaxRegions) continue;
			Count = FMath::Max(Count, Item->RegionIndex + 1);
			for (int32 Next : Item->Neighbours)
				if (Next >= 0 && Next < ForceGoals::MaxRegions && Region(State, Next))
					Graph[Item->RegionIndex] |= uint64(1) << Next;
		}
		return Count;
	}

	int32 SourceRegion(const ACommandBuilding& Building, const ACommandGameState& State)
	{
		int32 Joined = 0, Travelling = 0;
		Building.GetForceCounts(Joined, Travelling);
		const FVector Position = Joined > 0 && IsValid(Building.ForceGroup)
			? Building.ForceGroup->GetCenter() : Building.GetActorLocation();
		const AMapRegion* Source = State.FindRegionAt(Position);
		return Source ? Source->RegionIndex : INDEX_NONE;
	}

	int32 EnemyMain(const ACommandGameState& State, int32 Team)
	{
		int32 Result = INDEX_NONE;
		for (const AMapRegion* Item : State.Regions)
			if (IsValid(Item) && Item->RegionRole == ERegionRole::Main && Item->HomeTeam == (Team == 0 ? 5 : 0)
				&& (Result == INDEX_NONE || Item->RegionIndex < Result)) Result = Item->RegionIndex;
		return Result;
	}

	void BuildPath(FForceGoalDriver& Driver, int32 Source, int32 Target)
	{
		Driver.PathLength = Driver.PathCursor = 0;
		int32 Current = Source;
		while (Current != INDEX_NONE && Driver.PathLength < ForceGoals::MaxRegions)
		{
			Driver.Path[Driver.PathLength++] = Current;
			if (Current == Target) return;
			Current = ForceGoals::NextWaypoint(Driver.Graph, Driver.RegionCount, Current, Target);
		}
		Driver.PathLength = 0;
	}
}

bool ACommandBuilding::SetGoal(EForceGoal Goal, int32 RegionIndex)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || IsActorBeingDestroyed() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsAlive() || !IsComplete() || !IsProducer() || !bForceConfigured || !IsValid(ForceGroup)
		|| ForceGroup->IsActorBeingDestroyed() || !IsValid(OwningPlayerState)
		|| OwningPlayerState->TeamIndex != TeamIndex
		|| (Goal != EForceGoal::Hold && Goal != EForceGoal::Expand
			&& Goal != EForceGoal::Assault && Goal != EForceGoal::FallBack)) return false;
	const int32 Source = SourceRegion(*this, *State);
	if (Goal == EForceGoal::Assault)
	{
		if (RegionIndex != INDEX_NONE) return false;
		RegionIndex = EnemyMain(*State, TeamIndex);
	}
	else if (Goal == EForceGoal::FallBack)
	{
		if (RegionIndex != INDEX_NONE) return false;
		const AMapRegion* Home = State->FindRegionAt(GetActorLocation());
		RegionIndex = Home ? Home->RegionIndex : INDEX_NONE;
	}
	const AMapRegion* Target = Region(*State, RegionIndex);
	if (!Target || Source == INDEX_NONE || (Goal != EForceGoal::Assault
		&& Target->RegionRole == ERegionRole::Main && Target->HomeTeam != TeamIndex)) return false;
	uint64 Graph[ForceGoals::MaxRegions];
	const int32 Count = ReadGraph(*State, Graph);
	if (Goal != EForceGoal::FallBack
		&& ForceGoals::NextWaypoint(Graph, Count, Source, RegionIndex) == INDEX_NONE) return false;

	// Validation above is read-only: a rejected goal preserves the current goal, waypoint and front.
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
		if (Home && State->GetRegionController(Home->RegionIndex) == TeamIndex) GoalDriver.LastHeld = Home->RegionIndex;
	}
	GoalDriver.RegionCount = Count;
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index) GoalDriver.Graph[Index] = Graph[Index];
	TickGoal();
	ForceNetUpdate();
	return true;
}

void ACommandBuilding::TickGoal()
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || !State || State->MatchResult != EMatchResult::Ongoing
		|| !IsAlive() || IsActorBeingDestroyed() || !IsProducer()) return;
	if (!GoalDriver.bInitialized)
	{
		const AMapRegion* Home = State->FindRegionAt(GetActorLocation());
		if (!Home) return; // Level discovery can precede deferred producer spawning.
		GoalRegionIndex = Home->RegionIndex;
		GoalDriver.LastHeld = Home->RegionIndex;
		GoalDriver.RegionCount = ReadGraph(*State, GoalDriver.Graph);
		GoalDriver.bInitialized = true;
		GoalDriver.bEnabled = TeamIndex != 5; // Enemy planner still uses its internal location fronts.
		ForceNetUpdate();
	}
	if (!GoalDriver.bEnabled || !IsComplete() || !bForceConfigured || !IsValid(ForceGroup)
		|| ForceGroup->IsActorBeingDestroyed()) return;

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
	if (bAdvancing) GetForceCounts(Joined, Travelling);
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
				if (GoalDriver.Path[Index] == GoalDriver.LastHeld) { GoalDriver.PathCursor = Index; break; }
		}
		if (GoalDriver.bRefilling && State->GetRegionController(GoalDriver.LastHeld) != TeamIndex)
		{
			const AMapRegion* Home = State->FindRegionAt(GetActorLocation());
			GoalDriver.LastHeld = Home && State->GetRegionController(Home->RegionIndex) == TeamIndex
				? Home->RegionIndex : INDEX_NONE;
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
		if (GoalDriver.PathLength == 0) return;
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
		if (ForceGoal != EForceGoal::Hold) Order = EFrontOrder::Secure;
	}
	if (!Region(*State, Waypoint)) return;
	GoalDriver.Waypoint = Waypoint;
	// Wipes preserve this cache and the persistent group; replacement units inherit the front.
	if (GoalDriver.AppliedWaypoint == Waypoint && GoalDriver.AppliedOrder == static_cast<uint8>(Order)) return;
	const FVector Location = ForceGoal == EForceGoal::FallBack ? ForceGroup->GetHomeLocation() : State->GetRegionAnchor(Waypoint);
	if (!SetFront(Order, Location)) return;
	GoalDriver.bEnabled = true; // SetFront's opt-out belongs only to external internal-front callers.
	GoalDriver.AppliedWaypoint = Waypoint;
	GoalDriver.AppliedOrder = static_cast<uint8>(Order);
}
