#include "CommandService.h"
#include "ForceCapState.h"
#include "CommandBuilding.h"
#include "CommandGameMode.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "ArenaBounds.h"
#include "CombatTarget.h"
#include "Content/MatchContent.h"
#include "Engine/World.h"
#include "MapRegion.h"
#include "MatchTelemetry.h"
#include "OrderGraph.h"

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
	const UBuildingDefinition* Definition = IsValid(State->Content) ? State->Content->Building(BuildingIndex) : nullptr;
	if (Definition && Definition->bProducesForces)
	{
		FString CapReason = CommandForceCap::BlockReason(CommandForceCap::Read(*State, *Commander));
		if (!CapReason.IsEmpty())
			return Verdict(false, MoveTemp(CapReason));
	}
	FString Reason;
	ACommandBuilding* Building = State->ApplyPlacement(BuildingIndex, Location, Commander, Commander->TeamIndex, Reason);
	if (!Building)
		return Verdict(false, Reason.IsEmpty() ? FString(TEXT("Placement rejected by server.")) : MoveTemp(Reason));
	State->MatchTelemetry->RecordAccepted(Commander, EMatchDecision::Build);
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

FCommandResult FCommandService::IssueForceOrder(ACommandPlayerState* Commander, AArmyGroup* Force, EForceVerb Verb, int32 RegionIndex, AActor* Structure, bool bQueue)
{
	AArmyGroup* const Selection[] = { Force };
	return IssueForceOrder(Commander, Selection, Verb, RegionIndex, Structure, bQueue);
}

FCommandResult FCommandService::IssueForceOrder(ACommandPlayerState* Commander, TConstArrayView<AArmyGroup*> Forces, EForceVerb Verb, int32 RegionIndex, AActor* Structure, bool bQueue)
{
	const ACommandGameState* State = CommandState(Commander);
	if (!State || Forces.IsEmpty() || (Verb != EForceVerb::MoveHold && Verb != EForceVerb::Attack && Verb != EForceVerb::Retreat))
		return Verdict(false, TEXT("Order rejected: unavailable force or invalid verb."));
	if (Structure && (Verb != EForceVerb::Attack || !IsValid(Structure) || Structure->GetWorld() != Commander->GetWorld() || Cast<AArmyUnit>(Structure) || !CombatTarget::IsAliveHostile(Structure, Commander->TeamIndex)))
		return Verdict(false, TEXT("Attack requires a living hostile structure."));
	if (Structure)
	{
		const AMapRegion* Region = State->FindRegionAt(Structure->GetActorLocation());
		RegionIndex = Region ? Region->RegionIndex : INDEX_NONE;
	}
	if ((Verb == EForceVerb::Retreat && (RegionIndex != INDEX_NONE || Structure))
		|| (Verb != EForceVerb::Retreat && !ForceOrderGraph::Region(*State, RegionIndex)))
		return Verdict(false, TEXT("Order rejected: choose a valid region."));
	uint64 Graph[ForceOrders::MaxRegions];
	const int32 Count = ForceOrderGraph::ReadGraph(*State, Graph);
	float Speed = 0.f;
	for (int32 Index = 0; Index < Forces.Num(); ++Index)
	{
		AArmyGroup* Force = Forces[Index];
		if (!OwnsArmy(Commander, Force))
			return Verdict(false, TEXT("Order rejected: every selected force must belong to you."), ECommandRejection::InvalidOwner);
		for (int32 Previous = 0; Previous < Index; ++Previous)
			if (Forces[Previous] == Force)
				return Verdict(false, TEXT("Order rejected: duplicate selected force."));
		if (!ForceOrders::CanQueue(Force->Orders.Num(), bQueue))
			return Verdict(false, TEXT("Order rejected: three orders maximum, including the active order."));
		const int32 Source = ForceOrderGraph::SourceRegion(*Force, *State);
		const int32 Target = Verb == EForceVerb::Retreat ? ForceOrderGraph::TeamMain(*State, Commander->TeamIndex) : RegionIndex;
		if (ForceOrders::NextWaypoint(Graph, Count, Source, Target) == INDEX_NONE)
			return Verdict(false, TEXT("Order rejected: region is unreachable."));
		const float Speeds[] = { Speed, Force->GetBaseMarchSpeed() };
		Speed = ForceOrders::SlowestSpeed(MakeArrayView(Speeds));
	}
	for (AArmyGroup* Force : Forces)
		Force->CommitOrder(FForceOrder(Verb, RegionIndex, Structure), bQueue, Speed);
	State->MatchTelemetry->RecordAccepted(Commander, EMatchDecision::Order);
	return Verdict(true, TEXT("Force order accepted."));
}

FCommandResult FCommandService::SetRetreatThreshold(ACommandPlayerState* Commander, AArmyGroup* Force, ERetreatThreshold Threshold)
{
	AArmyGroup* const Selection[] = { Force };
	return SetRetreatThreshold(Commander, Selection, Threshold);
}

FCommandResult FCommandService::SetRetreatThreshold(ACommandPlayerState* Commander, TConstArrayView<AArmyGroup*> Forces, ERetreatThreshold Threshold)
{
	if (Forces.IsEmpty() || (Threshold != ERetreatThreshold::Never && Threshold != ERetreatThreshold::Percent25 && Threshold != ERetreatThreshold::Percent40 && Threshold != ERetreatThreshold::Percent60))
		return Verdict(false, TEXT("Retreat threshold rejected: invalid setting."));
	for (const AArmyGroup* Force : Forces)
		if (!OwnsArmy(Commander, Force))
			return Verdict(false, TEXT("Retreat threshold rejected: not your force."), ECommandRejection::InvalidOwner);
	for (AArmyGroup* Force : Forces)
	{
		Force->RetreatThreshold = Threshold;
		Force->ForceNetUpdate();
	}
	return Verdict(true, TEXT("Attack retreat threshold set."));
}

FCommandResult FCommandService::SetRallyPoint(ACommandPlayerState* Commander, ACommandBuilding* Building, int32 RegionIndex)
{
	if (!OwnsBuilding(Commander, Building) || !Building->IsProducer())
		return Verdict(false, TEXT("Rally rejected: not your production building."), ECommandRejection::InvalidOwner);
	const ACommandGameState* State = CommandState(Commander);
	const AMapRegion* Source = State->FindRegionAt(Building->GetActorLocation());
	uint64 Graph[ForceOrders::MaxRegions];
	const int32 Count = ForceOrderGraph::ReadGraph(*State, Graph);
	if (!Source || !ForceOrderGraph::Region(*State, RegionIndex)
		|| ForceOrders::NextWaypoint(Graph, Count, Source->RegionIndex, RegionIndex) == INDEX_NONE)
		return Verdict(false, TEXT("Rally rejected: invalid or unreachable region."));
	Building->RallyRegionIndex = RegionIndex;
	Building->ForceNetUpdate();
	if (AArmyGroup* Force = Building->ForceGroup; IsValid(Force))
		Force->RetargetIdleRally(RegionIndex);
	return Verdict(true, TEXT("Production rally point set."));
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
FCommandResult FCommandService::Pause(ACommandPlayerController* Controller)
{
	ACommandPlayerState* Commander = IsValid(Controller) ? Controller->GetPlayerState<ACommandPlayerState>() : nullptr;
	ACommandGameState* State = CommandState(Commander);
	if (!State || Commander->TeamIndex != 0)
		return Verdict(false, TEXT("Pause unavailable: no ongoing battle."), ECommandRejection::Unavailable);
	if (State->GetNetMode() != NM_Standalone && State->IsCoopPauseSpent())
		return Verdict(false, TEXT("Team pause spent: one pause per battle."));
	const bool bAccepted = State->ApplyPause(Controller, true);
	return Verdict(bAccepted, bAccepted ? TEXT("Paused: orders apply now; simulation continues on resume.") : TEXT("Battle is already paused."));
}

FCommandResult FCommandService::Resume(ACommandPlayerController* Controller)
{
	ACommandPlayerState* Commander = IsValid(Controller) ? Controller->GetPlayerState<ACommandPlayerState>() : nullptr;
	ACommandGameState* State = CommandState(Commander);
	if (!State || Commander->TeamIndex != 0)
		return Verdict(false, TEXT("Resume unavailable: no ongoing battle."), ECommandRejection::Unavailable);
	const bool bAccepted = State->ApplyPause(Controller, false);
	return Verdict(bAccepted, bAccepted ? TEXT("Battle resumed.") : TEXT("Battle is not actively paused."));
}
