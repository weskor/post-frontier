#include "ArmyGroup.h"

#include "AIController.h"
#include "ArenaBounds.h"
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

bool PrepareMove(UNavigationSystemV1& Navigation, const FNavAgentProperties& Agent, UObject* Querier,
	UPathFollowingComponent& Following, const FVector& Start, const FVector& Target,
	FPreparedMove& Prepared, float ProjectionRadius)
{
	if (!AArenaBounds::IsTravelLocation(Navigation.GetWorld(), Target))
	{
		return false;
	}
	const ANavigationData* NavData = Navigation.GetNavDataForProps(Agent, Start);
	FNavLocation Projected;
	if (!NavData || !Navigation.ProjectPointToNavigation(Target, Projected, FVector(ProjectionRadius, ProjectionRadius, 200.0f), NavData)
		|| !AArenaBounds::IsTravelLocation(Navigation.GetWorld(), Projected.Location)
		|| FVector::DistSquared2D(Target, Projected.Location) > FMath::Square(ProjectionRadius))
	{
		return false;
	}
	FPathFindingQuery Query(Querier, *NavData, Start, Projected.Location);
	Query.SetAllowPartialPaths(false);
	// Match the engine's normal query setup, including Detour crowd's
	// corridor-preserving flags, before validating the synchronous path.
	Following.OnPathfindingQuery(Query);
	const FPathFindingResult Result = Navigation.FindPathSync(Agent, Query);
	if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
	{
		return false;
	}
	Prepared.Goal = Projected.Location;
	Prepared.Path = Result.Path;
	return true;
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

// A slot that has no path of its own sends its unit to the nearest navigable point within this distance of it.
constexpr float SlotFallbackRadius = 300.f;

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

// Validates the entire command without touching the current order or paths.
// In particular, partial paths never count as accepting the user's target. The centre check is strict; the
// slots are the force's formation fitted inside the region around that centre (ArmyGroupPolicy::FitFormation),
// and a region order gives each slot its own fallback.
bool PrepareFormationMoves(UNavigationSystemV1& Navigation, const ACommandGameState* State,
	const TArray<TObjectPtr<AArmyUnit>>& Units, const ArmyGroupPolicy::FFormation& Formation,
	const FVector& Destination, FPreparedMoves& Prepared, FVector& ProjectedCenter)
{
	const AMapRegion* Region = nullptr;
	TConstArrayView<FVector2D> Polygon;
	ArmyGroupPolicy::FFit Fit;
	bool bCentred = false, bFallback = false;
	for (int32 Index = 0; Index < Units.Num(); ++Index)
	{
		AArmyUnit* Unit = Units[Index];
		if (!IsValid(Unit) || !Unit->IsAlive())
			continue;
		AAIController* AI = GetReadyController(Unit);
		if (!AI)
		{
			return false;
		}
		if (!bCentred)
		{
			FPreparedMove CenterMove;
			if (!PrepareMove(Navigation, Unit->GetNavAgentPropertiesRef(), AI, *AI->GetPathFollowingComponent(),
					Unit->GetNavAgentLocation(), Destination, CenterMove, 75.0f))
			{
				return false;
			}
			ProjectedCenter = CenterMove.Goal;
			Region = State ? State->FindRegionAt(ProjectedCenter) : nullptr;
			if (Region)
			{
				Polygon = Region->Polygon;
				bFallback = ArmyGroupPolicy::IsRegionOrderDestination(Destination, State->GetRegionAnchor(Region->RegionIndex));
			}
			Fit = ArmyGroupPolicy::FitForce(Formation, Polygon, ProjectedCenter);
			bCentred = true;
		}
		FPreparedMove Move;
		const FVector Slot = ArmyGroupPolicy::FittedSlot(Formation, Fit, Polygon, Unit->GetCompositionSlot());
		if (!PrepareSlotMove(Navigation, *Unit, *AI, Slot, ProjectedCenter, Region, bFallback, Move))
		{
			return false;
		}
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
	if (!PrepareFormationMoves(*Navigation, State, Units, FormationShape(), InDestination, Prepared, ProjectedCenter)
		|| (Prepared.IsEmpty() && !ValidateAssemblyRoute(*Navigation, *this, HomeLocation, InDestination, ProjectedCenter)))
		return false;
	if (!bApply)
		return true;

	StopAllUnits();
	ResetHoldState();
	for (const FPreparedMove& Move : Prepared)
	{
		// Submit the complete prevalidated path; no second query or delayed order.
		StartPreparedMove(Move);
	}
	Order = NewOrder;
	AttackTarget = nullptr;
	Destination = ProjectedCenter;
	++OrderSerial;
	ForceNetUpdate();
	LogOrder();
	return true;
}
