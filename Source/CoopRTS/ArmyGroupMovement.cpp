#include "ArmyGroup.h"

#include "AIController.h"
#include "ArenaBounds.h"
#include "ArmyGroupPathing.h"
#include "ArmyGroupInternal.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "MapRegion.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "Navigation/PathFollowingComponent.h"

namespace ArmyGroupInternal
{
AAIController* GetReadyController(AArmyUnit* Unit)
{
	AAIController* AI = IsValid(Unit) ? Cast<AAIController>(Unit->GetController()) : nullptr;
	UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	return Following && !Following->IsResourceLocked() && Following->IsPathFollowingAllowed() ? AI : nullptr;
}

namespace
{
// The query for a move to Target, or false when Target is outside the arena or too far from the navmesh (the
// projection of Target within ProjectionRadius is what the move goes to).
bool BuildMoveQuery(UNavigationSystemV1& Navigation, const FNavAgentProperties& Agent, UObject* Querier,
	UPathFollowingComponent& Following, const FVector& Start, const FVector& Target, float ProjectionRadius,
	const ANavigationData*& OutNavData, FPathFindingQuery& OutQuery)
{
	if (!AArenaBounds::IsTravelLocation(Navigation.GetWorld(), Target))
		return false;
	const ANavigationData* NavData = Navigation.GetNavDataForProps(Agent, Start);
	FNavLocation Projected;
	if (!NavData || !Navigation.ProjectPointToNavigation(Target, Projected, FVector(ProjectionRadius, ProjectionRadius, 200.0f), NavData)
		|| !AArenaBounds::IsTravelLocation(Navigation.GetWorld(), Projected.Location)
		|| FVector::DistSquared2D(Target, Projected.Location) > FMath::Square(ProjectionRadius))
		return false;
	OutNavData = NavData;
	OutQuery = FPathFindingQuery(Querier, *NavData, Start, Projected.Location);
	OutQuery.SetAllowPartialPaths(false);
	// Match the engine's normal query setup, including Detour crowd's
	// corridor-preserving flags, before validating the synchronous path.
	Following.OnPathfindingQuery(OutQuery);
	return true;
}

bool SolveMove(UNavigationSystemV1& Navigation, const FNavAgentProperties& Agent, const ANavigationData& NavData,
	const FPathFindingQuery& Query, bool bStraightFirst, FPreparedMove& Prepared)
{
	FNavPathSharedPtr Path;
	if (bStraightFirst && ArmyGroupPathing::TryStraightPath(NavData, Query, Path))
		ArmyGroupPathing::NoteStraightMove();
	else
	{
		ArmyGroupPathing::NotePathQuery();
		const FPathFindingResult Result = Navigation.FindPathSync(Agent, Query);
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
			return false;
		Path = Result.Path;
	}
	Prepared.Goal = Query.EndLocation;
	Prepared.Path = Path;
	return true;
}
}

bool PrepareMove(UNavigationSystemV1& Navigation, const FNavAgentProperties& Agent, UObject* Querier,
	UPathFollowingComponent& Following, const FVector& Start, const FVector& Target,
	FPreparedMove& Prepared, float ProjectionRadius)
{
	const ANavigationData* NavData = nullptr;
	FPathFindingQuery Query;
	return BuildMoveQuery(Navigation, Agent, Querier, Following, Start, Target, ProjectionRadius, NavData, Query)
		&& SolveMove(Navigation, Agent, *NavData, Query, false, Prepared);
}

namespace
{
// A slot's route: a clear straight line answers without a path query (see ArmyGroupPathing::TryStraightPath),
// a blocked one is a path query as PrepareMove would make.
bool PrepareSlotRoute(UNavigationSystemV1& Navigation, const FNavAgentProperties& Agent, UObject* Querier,
	UPathFollowingComponent& Following, const FVector& Start, const FVector& Target,
	FPreparedMove& Prepared, float ProjectionRadius)
{
	const ANavigationData* NavData = nullptr;
	FPathFindingQuery Query;
	return BuildMoveQuery(Navigation, Agent, Querier, Following, Start, Target, ProjectionRadius, NavData, Query)
		&& SolveMove(Navigation, Agent, *NavData, Query, true, Prepared);
}
}

bool StartPreparedMove(const FPreparedMove& Move)
{
	FAIMoveRequest Request(Move.Goal);
	Request.SetAcceptanceRadius(35.0f);
	Request.SetReachTestIncludesAgentRadius(false);
	Request.SetAllowPartialPath(false);
	return Move.Controller->RequestMove(Request, Move.Path).IsValid();
}

void DestroyUnit(AArmyUnit* Unit)
{
	if (IsValid(Unit))
	{
		if (AController* Controller = Unit->GetController())
		{
			Controller->Destroy();
		}
		Unit->Destroy();
	}
}
}

using namespace ArmyGroupInternal;

namespace
{
using FPreparedMoves = TArray<FPreparedMove, TInlineAllocator<MaxUnitCount>>;

using ArmyGroupPolicy::SlotFallbackRadius;

// One unit's move to its fitted slot. The slot itself first. A region order then falls back to the nearest
// navigable point around the slot that stays inside the region, then to the force's centre, which the caller
// has already proved reachable for the first member; only when even the centre has no path for this unit does
// the command fail. A precise point order has no fallback: an obstructed formation rejects it.
bool PrepareSlotMove(UNavigationSystemV1& Navigation, AArmyUnit& Unit, AAIController& AI, const FVector& Slot,
	const FVector& Centre, const AMapRegion* Region, bool bFallback, FPreparedMove& Move)
{
	const FNavAgentProperties& Agent = Unit.GetNavAgentPropertiesRef();
	const FVector Start = Unit.GetNavAgentLocation();
	UPathFollowingComponent& Following = *AI.GetPathFollowingComponent();
	Move.Controller = &AI;
	// Limit horizontal projection so a wall cannot collapse several slots
	// onto the same edge, or pull an out-of-arena slot back into bounds.
	if (PrepareMove(Navigation, Agent, &AI, Following, Start, Slot, Move))
		return true;
	if (!bFallback)
		return false;
	FPreparedMove Nearby;
	if (PrepareMove(Navigation, Agent, &AI, Following, Start, Slot, Nearby, SlotFallbackRadius)
		&& (!Region || Region->Contains(Nearby.Goal)))
	{
		Nearby.Controller = &AI;
		Move = MoveTemp(Nearby);
		return true;
	}
	return PrepareMove(Navigation, Agent, &AI, Following, Start, Centre, Move, 75.0f);
}

// What the leg being ordered is, for choosing the shape and heading of its slots. Seed varies a column's jitter
// between forces (stable for one force).
struct FLegContext
{
	const ACommandGameState* State = nullptr;
	int32 TargetRegion = INDEX_NONE;
	bool bMarching = false;
	double Now = 0.;
	int32 Seed = 0;
};

using FLivingUnits = TArray<AArmyUnit*, TInlineAllocator<MaxUnitCount>>;

// The shape and heading of a leg to Center and its destination slots: the box at arrival and at short legs, a
// column on an intermediate leg of a long march (ArmyGroupPolicy::ChooseLeg keeps the previous plan's heading and
// shape against small changes, on the members' memory), each unit assigned by least total distance and class row.
// Planned is the memory the order leaves on its members.
ArmyGroupPolicy::FLegPlan PlanLegTargets(const FLegContext& Leg, const FLivingUnits& Living,
	const ArmyGroupPolicy::FFormation& Formation, TConstArrayView<FVector2D> Polygon, const AMapRegion* Region,
	const FVector& Center, ArmyGroupPolicy::FLegMemory& Planned)
{
	FVector Mean = FVector::ZeroVector;
	TArray<ArmyGroupPolicy::FMarchUnit, TInlineAllocator<MaxUnitCount>> March;
	for (const AArmyUnit* Unit : Living)
	{
		Mean += Unit->GetActorLocation() / Living.Num();
		March.Add({ FVector2D(Unit->GetActorLocation()), Unit->FormationClassRank(), Unit->GetCompositionSlot() });
	}
	for (const AArmyUnit* Unit : Living)
		if (Unit->FormationMemory.bPlanned)
		{
			Planned = Unit->FormationMemory;
			break;
		}
	const FVector2D Way = FVector2D(Center - Mean);
	const float Desired = Way.SizeSquared() > 1. ? FMath::Atan2(Way.Y, Way.X) : 0.f;
	const bool bIntermediate = Region && Leg.TargetRegion != INDEX_NONE && Region->RegionIndex != Leg.TargetRegion;
	const ArmyGroupPolicy::FLegChoice Choice = ArmyGroupPolicy::ChooseLeg(Planned, FVector2D(Center), Desired,
		Leg.bMarching, bIntermediate, FVector::Dist2D(Mean, Center), Leg.Now);
	ArmyGroupPolicy::FLegPlan Plan = ArmyGroupPolicy::PlanLeg(Formation, Polygon, Center, March, Choice.Shape, Choice.Yaw, Leg.Seed);
	Planned.bColumn = Plan.Shape == ArmyGroupPolicy::ELegShape::Column;
	return Plan;
}

// Validates the entire command without touching the current order or paths.
// In particular, partial paths never count as accepting the user's target. The centre check is strict; the
// slots are the leg's plan (ArmyGroupPolicy::PlanLeg: the formation fitted inside the region around that
// centre, facing the heading), and a region order gives each slot its own fallback.
bool PrepareFormationMoves(UNavigationSystemV1& Navigation, const FLegContext& Leg,
	const TArray<TObjectPtr<AArmyUnit>>& Units, const ArmyGroupPolicy::FFormation& Formation,
	const FVector& Destination, FPreparedMoves& Prepared, FVector& ProjectedCenter, ArmyGroupPolicy::FLegMemory& Planned)
{
	FLivingUnits Living;
	for (AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive())
		{
			if (!GetReadyController(Unit))
				return false;
			Living.Add(Unit);
		}
	if (Living.IsEmpty())
		return true;
	AAIController* First = GetReadyController(Living[0]);
	FPreparedMove CenterMove;
	if (!PrepareMove(Navigation, Living[0]->GetNavAgentPropertiesRef(), First, *First->GetPathFollowingComponent(),
			Living[0]->GetNavAgentLocation(), Destination, CenterMove, 75.0f))
		return false;
	ProjectedCenter = CenterMove.Goal;
	const AMapRegion* Region = Leg.State ? Leg.State->FindRegionAt(ProjectedCenter) : nullptr;
	TConstArrayView<FVector2D> Polygon;
	if (Region)
		Polygon = Region->Polygon;
	const bool bFallback = Region
		&& ArmyGroupPolicy::IsRegionOrderDestination(Destination, Leg.State->GetRegionAnchor(Region->RegionIndex));
	const ArmyGroupPolicy::FLegPlan Plan = PlanLegTargets(Leg, Living, Formation, Polygon, Region, ProjectedCenter, Planned);
	for (int32 Index = 0; Index < Living.Num(); ++Index)
	{
		FPreparedMove Move;
		if (!PrepareSlotMove(Navigation, *Living[Index], *GetReadyController(Living[Index]), Plan.Targets[Index],
				ProjectedCenter, Region, bFallback, Move))
			return false;
		Prepared.Add(MoveTemp(Move));
	}
	return true;
}

// A wiped/assembling force still owns its intent. Validate a complete route
// from assembly rather than inventing a member or accepting a partial path.
bool ValidateAssemblyRoute(UNavigationSystemV1& Navigation, UObject& Querier, const FVector& Home,
	const FVector& Destination, FVector& ProjectedCenter)
{
	const FNavAgentProperties& Agent = GetDefault<AArmyUnit>()->GetNavAgentPropertiesRef();
	const ANavigationData* NavData = Navigation.GetNavDataForProps(Agent, Home);
	FNavLocation Projected;
	if (!NavData || !Navigation.ProjectPointToNavigation(Destination, Projected, FVector(75.f, 75.f, 200.f), NavData) || !AArenaBounds::IsTravelLocation(Querier.GetWorld(), Projected.Location)
		|| FVector::DistSquared2D(Destination, Projected.Location) > FMath::Square(75.f))
		return false;
	FPathFindingQuery Query(&Querier, *NavData, Home, Projected.Location);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult Result = Navigation.FindPathSync(Agent, Query);
	if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
		return false;
	ProjectedCenter = Projected.Location;
	return true;
}
}

void AArmyGroup::StopAllUnits()
{
	// Abort every old request before submitting any replacement. There are no
	// completion delegates or delayed regroup orders that can resurrect intent.
	for (AArmyUnit* Unit : Units)
	{
		if (!IsValid(Unit))
		{
			continue;
		}
		if (AAIController* AI = Cast<AAIController>(Unit->GetController()))
		{
			AI->StopMovement();
			AI->ClearFocus(EAIFocusPriority::Gameplay);
		}
		Unit->GetCharacterMovement()->StopMovementImmediately();
		Unit->Target = nullptr;
		Unit->bPursuing = false;
	}
}

bool AArmyGroup::IssueTravel(EArmyOrder NewOrder, const FVector& InDestination, bool bApply)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!HasAuthority() || (Units.IsEmpty() && !IsValid(ProductionBuilding) && !bProducedGroup)
		|| !AArenaBounds::IsTravelLocation(GetWorld(), InDestination) || (State && State->MatchResult != EMatchResult::Ongoing))
	{
		return false;
	}

	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GetWorld());
	if (!Navigation)
	{
		return false;
	}

	FPreparedMoves Prepared;
	FVector ProjectedCenter = FVector::ZeroVector;
	ArmyGroupPolicy::FLegMemory Planned;
	const double Now = GetWorld()->GetTimeSeconds();
	const int32 Seed = static_cast<int32>(HashCombineFast(GetTypeHash(ArmyIndex), GetTypeHash(ForceNumber)));
	const FLegContext Leg{ State, TargetRegionIndex, Status == EForceStatus::Marching, Now, Seed };
	bool bPlanned = false;
	{
		const ArmyGroupPathing::FOrderCount Cost;
		bPlanned = PrepareFormationMoves(*Navigation, Leg, Units, FormationShape(), InDestination, Prepared, ProjectedCenter, Planned)
			&& (!Prepared.IsEmpty() || ValidateAssemblyRoute(*Navigation, *this, HomeLocation, InDestination, ProjectedCenter));
		const ArmyGroupPathing::FQueryStats& Total = ArmyGroupPathing::Snapshot();
		UE_LOG(LogOrderCost, Display, TEXT("%s order cost planned=%d apply=%d path_queries=%lld straight_moves=%lld units=%d total_path_queries=%lld total_straight_moves=%lld"),
			*GetName(), bPlanned, bApply, Cost.PathQueries(), Cost.StraightMoves(), Units.Num(), Total.PathQueries, Total.StraightMoves);
	}
	if (!bPlanned)
		return false;
	if (!bApply)
		return true;

	StopAllUnits();
	ResetHoldState();
	for (const FPreparedMove& Move : Prepared)
	{
		// Submit the complete prevalidated path; no second query or delayed order.
		StartPreparedMove(Move);
		if (AArmyUnit* Member = Move.Controller ? Cast<AArmyUnit>(Move.Controller->GetPawn()) : nullptr)
			Member->FormationTarget = Move.Goal;
	}
	for (AArmyUnit* Unit : Units)
		if (IsValid(Unit) && Unit->IsAlive())
			Unit->FormationMemory = Planned;
	Order = NewOrder;
	AttackTarget = nullptr;
	Destination = ProjectedCenter;
	++OrderSerial;
	ForceNetUpdate();
	LogOrder();
	return true;
}
