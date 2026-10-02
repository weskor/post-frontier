#include "CommandService.h"
#include "CommandBuilding.h"
#include "CommandGameMode.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "ArenaBounds.h"
#include "CombatTarget.h"
#include "Content/MatchContent.h"
#include "Engine/World.h"
#include "MapRegion.h"
#include "GoalGraph.h"

namespace
{
ACommandGameState* CommandState(ACommandPlayerState* Commander)
{
	if (!IsValid(Commander) || !Commander->HasAuthority() || !Commander->GetWorld())
		return nullptr;
	ACommandGameState* State = Commander->GetWorld()->GetGameState<ACommandGameState>();
	if (!State || State->MatchResult != EMatchResult::Ongoing
		|| (Commander->TeamIndex == 0 ? Commander->CommanderIndex < 0 || Commander->CommanderIndex >= 5
									  : Commander->TeamIndex != 5 || State->EnemyCommander != Commander))
		return nullptr;
	return State;
}

bool OwnsBuilding(ACommandPlayerState* Commander, const ACommandBuilding* Building)
{
	return CommandState(Commander) && IsValid(Building) && !Building->IsActorBeingDestroyed()
		&& Building->GetWorld() == Commander->GetWorld() && Building->IsAlive()
		&& Building->TeamIndex == Commander->TeamIndex && Building->OwningPlayerState == Commander;
}

bool OwnsArmy(ACommandPlayerState* Commander, const AArmyGroup* Army)
{
	return CommandState(Commander) && IsValid(Army) && !Army->IsActorBeingDestroyed()
		&& Army->GetWorld() == Commander->GetWorld() && Army->GetTeamIndex() == Commander->TeamIndex
		&& Army->GetOwningPlayerState() == Commander
		&& (Commander->TeamIndex == 5 || Army->GetOwner() == Commander->GetOwner());
}

FCommandResult Verdict(bool bAccepted, FString Message, ECommandRejection Failure = ECommandRejection::InvalidRequest)
{
	return { bAccepted ? ECommandRejection::None : Failure, MoveTemp(Message), nullptr };
}
}

FCommandResult FCommandService::PlaceBuilding(ACommandPlayerState* Commander, int32 BuildingIndex, const FVector& Location)
{
	ACommandGameState* State = CommandState(Commander);
	if (!State)
		return Verdict(false, TEXT("Placement rejected: match or commander unavailable."), ECommandRejection::Unavailable);
	FString Reason;
	ACommandBuilding* Building = State->ApplyPlacement(BuildingIndex, Location, Commander, Commander->TeamIndex, Reason);
	if (!Building)
		return Verdict(false, Reason.IsEmpty() ? FString(TEXT("Placement rejected by server.")) : MoveTemp(Reason));
	return { ECommandRejection::None, TEXT("Building placed; construction started."), Building };
}

FCommandResult FCommandService::CancelBuilding(ACommandPlayerState* Commander, ACommandBuilding* Building)
{
	if (!OwnsBuilding(Commander, Building))
		return Verdict(false, TEXT("Cancel rejected: not your living building or match ended."), ECommandRejection::InvalidOwner);
	const bool bAccepted = Building->ApplyCancellation();
	return Verdict(bAccepted, bAccepted ? TEXT("Construction cancelled; unbuilt portion refunded.") : TEXT("Cancel rejected: building is complete."));
}

FCommandResult FCommandService::ConfigureProduction(ACommandPlayerState* Commander, ACommandBuilding* Building, EUnitRole Recipe, bool bEnabled)
{
	if (!OwnsBuilding(Commander, Building))
		return Verdict(false, TEXT("Production rejected: not your living building or match ended."), ECommandRejection::InvalidOwner);
	const ACommandGameState* State = CommandState(Commander);
	const int32 UnitIndex = IsValid(State->Content) ? State->Content->UnitIndexForRole(Recipe) : INDEX_NONE;
	const bool bAccepted = Building->ApplyProduction(UnitIndex, bEnabled);
	const FString StateName = StaticEnum<EProductionState>()->GetNameStringByValue(static_cast<int64>(Building->GetProductionState()));
	return Verdict(bAccepted, bAccepted ? FString::Printf(TEXT("%s: %s"), bEnabled ? TEXT("Enabled") : TEXT("Paused"), *StateName) : FString::Printf(TEXT("Production rejected: %s"), *StateName));
}

FCommandResult FCommandService::AssignGoal(ACommandPlayerState* Commander, ACommandBuilding* Building, EForceGoal Goal, int32 RegionIndex)
{
	if (!OwnsBuilding(Commander, Building))
		return Verdict(false, TEXT("Goal rejected: requires your completed, locked barracks in an ongoing match."), ECommandRejection::InvalidOwner);
	if (!Building->IsProducer() || !Building->IsComplete() || !Building->bForceConfigured || !IsValid(Building->ForceGroup))
		return Verdict(false, TEXT("Goal rejected: requires your completed, locked barracks in an ongoing match."), ECommandRejection::InvalidRequest);
	const ACommandGameState* State = CommandState(Commander);
	const auto Reject = [] { return Verdict(false, TEXT("Goal rejected: invalid or unreachable region, enemy main, or unavailable force.")); };
	if (Building->ForceGroup->IsActorBeingDestroyed()
		|| (Goal != EForceGoal::Hold && Goal != EForceGoal::Expand && Goal != EForceGoal::Assault && Goal != EForceGoal::FallBack))
		return Reject();
	const int32 Source = CommandGoalGraph::SourceRegion(*Building, *State);
	if (Goal == EForceGoal::Assault || Goal == EForceGoal::FallBack)
	{
		if (RegionIndex != INDEX_NONE)
			return Reject();
		const AMapRegion* Home = State->FindRegionAt(Building->GetActorLocation());
		RegionIndex = Goal == EForceGoal::Assault ? CommandGoalGraph::EnemyMain(*State, Building->TeamIndex)
			: Home                                ? Home->RegionIndex
												  : INDEX_NONE;
	}
	const AMapRegion* Target = CommandGoalGraph::Region(*State, RegionIndex);
	if (!Target || Source == INDEX_NONE || (Goal != EForceGoal::Assault && Target->RegionRole == ERegionRole::Main && Target->HomeTeam != Building->TeamIndex))
		return Reject();
	uint64 Graph[ForceGoals::MaxRegions];
	const int32 Count = CommandGoalGraph::ReadGraph(*State, Graph);
	if (Goal != EForceGoal::FallBack && ForceGoals::NextWaypoint(Graph, Count, Source, RegionIndex) == INDEX_NONE)
		return Reject();
	Building->CommitGoal(Goal, RegionIndex, Source, Graph, Count);
	return Verdict(true, TEXT("Goal assigned to this building's force."));
}

FCommandResult FCommandService::AssignFront(ACommandPlayerState* Commander, ACommandBuilding* Building, EFrontOrder Order, const FVector& Location)
{
	if (!OwnsBuilding(Commander, Building))
		return Verdict(false, TEXT("Front rejected: not your living building or match ended."), ECommandRejection::InvalidOwner);
	const bool bAccepted = Building->ApplyFront(Order, Location);
	return Verdict(bAccepted, bAccepted ? TEXT("Front assigned to this building's force.") : TEXT("Front rejected: select a completed barracks and valid ground inside the arena."));
}

FCommandResult FCommandService::AssignFront(ACommandPlayerState* Commander, AArmyGroup* Army, EFrontOrder Order, const FVector& Location)
{
	const bool bAccepted = OwnsArmy(Commander, Army) && Army->AssignFront(Order, Location);
	return Verdict(bAccepted, bAccepted ? TEXT("Front assigned to this building's force.") : TEXT("Front rejected: select a completed barracks and valid ground inside the arena."));
}

FCommandResult FCommandService::Research(ACommandPlayerState* Commander, ACommandBuilding* Building, EArmyDoctrine Choice)
{
	if (!OwnsBuilding(Commander, Building))
		return Verdict(false, TEXT("Research rejected: not your living building or match ended."), ECommandRejection::InvalidOwner);
	const bool bAccepted = Building->ApplyResearch(Choice);
	return Verdict(bAccepted, bAccepted ? FString(TEXT("Workshop specialization purchased for your forces.")) : FString::Printf(TEXT("Research rejected: requires completed workshop, no existing specialization, and %d resources."), ACommandBuilding::ResearchCost));
}

FCommandResult FCommandService::Restart(ACommandPlayerController* Controller)
{
	if (!IsValid(Controller) || !Controller->HasAuthority() || !Controller->GetWorld())
		return Verdict(false, TEXT("Restart rejected: match or commander unavailable."), ECommandRejection::Unavailable);
	const ACommandGameState* State = Controller->GetWorld()->GetGameState<ACommandGameState>();
	ACommandGameMode* Mode = Controller->GetWorld()->GetAuthGameMode<ACommandGameMode>();
	if (!State || State->MatchResult == EMatchResult::Ongoing || !Mode)
		return Verdict(false, TEXT("Restart rejected: match is not over."));
	const ACommandPlayerState* Commander = Controller->GetPlayerState<ACommandPlayerState>();
	if (Mode->bRestartRequested || !Commander || Commander->TeamIndex != 0
		|| Commander->CommanderIndex < 0 || Commander->CommanderIndex >= 5)
		return Verdict(false, TEXT("Restart rejected: match or commander unavailable."), ECommandRejection::Unavailable);
	// Seamless travel retains the net driver and player connections; the new world's actors are fresh.
	// Explicit ?SeamlessTravel avoids the engine's automatic hard-travel fallback after 48 hours.
	Mode->bRestartRequested = true;
	UWorld* World = Controller->GetWorld();
	const FString Map = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	const FString Options = World->GetNetMode() == NM_Standalone ? TEXT("?SeamlessTravel") : TEXT("?listen?SeamlessTravel");
	if (!World->ServerTravel(Map + Options, false))
	{
		Mode->bRestartRequested = false;
		return Verdict(false, TEXT("Restart rejected: travel unavailable."), ECommandRejection::Unavailable);
	}
	return Verdict(true, FString());
}

FCommandResult FCommandService::IssueOrder(ACommandPlayerState* Commander, AArmyGroup* Army, EArmyOrder Order, const FVector& Destination)
{
	const ACommandGameState* State = CommandState(Commander);
	bool bAccepted = false;
	if (State && OwnsArmy(Commander, Army) && IsValid(State->Arena) && State->Arena->ContainsTravel(Destination))
	{
		switch (Order)
		{
		case EArmyOrder::Move:
			bAccepted = Army->IssueTravel(Order, Destination);
			break;
		case EArmyOrder::Hold:
			bAccepted = !Army->GetUnits().IsEmpty() && Army->ApplyHold();
			break;
		case EArmyOrder::Retreat:
			bAccepted = Army->IssueTravel(Order, Army->GetHomeLocation());
			break;
		default:
			break;
		}
	}
	return Verdict(bAccepted, bAccepted ? TEXT("Manual order accepted; automatic front disabled for this squad.") : TEXT("Order rejected: choose reachable ground inside the arena."));
}

FCommandResult FCommandService::IssueAttack(ACommandPlayerState* Commander, AArmyGroup* Army, FVector Destination, AActor* Target)
{
	const ACommandGameState* State = CommandState(Commander);
	bool bAccepted = false;
	if (State && OwnsArmy(Commander, Army))
	{
		bool bValidTarget = !Target;
		if (Target && IsValid(Target) && Target->GetWorld() == Commander->GetWorld())
		{
			bValidTarget = CombatTarget::IsAliveHostile(Target, Army->GetTeamIndex());
			if (const AArmyUnit* Unit = Cast<AArmyUnit>(Target); bValidTarget && Unit)
				bValidTarget = Unit->GetGroup()->GetWorld() == Commander->GetWorld();
			if (bValidTarget)
				Destination = Target->GetActorLocation();
		}
		if (bValidTarget && IsValid(State->Arena) && State->Arena->ContainsTravel(Destination))
			bAccepted = Army->ApplyAttack(Destination, Target);
	}
	return Verdict(bAccepted, bAccepted ? TEXT("Attack order accepted; manual order overrides automatic front.") : TEXT("Attack rejected: choose a live enemy unit, building, HQ or reachable ground."));
}
