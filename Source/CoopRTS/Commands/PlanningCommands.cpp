#include "Commands/PlanningCommands.h"

#include "CombatTarget.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "Content/MatchContent.h"
#include "Engine/World.h"
#include "MapRegion.h"
#include "MatchTelemetry.h"
#include "Rules/ForceOrderPolicy.h"
#include "Rules/PlanningPolicy.h"

namespace
{
FCommandResult Verdict(bool bAccepted, FString Message, ECommandRejection Failure = ECommandRejection::InvalidRequest)
{
	return { bAccepted ? ECommandRejection::None : Failure, MoveTemp(Message), nullptr };
}

ACommandGameState* PlanningState(const ACommandPlayerState* Commander)
{
	if (!IsValid(Commander) || !Commander->HasAuthority() || !Commander->GetWorld() || Commander->TeamIndex != 0
		|| Commander->CommanderIndex < 0)
		return nullptr;
	ACommandGameState* State = Commander->GetWorld()->GetGameState<ACommandGameState>();
	return State && State->MatchResult == EMatchResult::Ongoing ? State : nullptr;
}

// The commander's kit when planning is on and they may edit it; otherwise null with the refusal in Refusal.
FPlanningKit* EditableKit(ACommandPlayerState* Commander, FCommandResult& Refusal)
{
	ACommandGameState* State = PlanningState(Commander);
	if (!State)
	{
		Refusal = Verdict(false, TEXT("Planning rejected: match or commander unavailable."), ECommandRejection::Unavailable);
		return nullptr;
	}
	FPlanningKit* Kit = State->FindKit(Commander);
	if (!Kit && !State->IsPlanning())
	{
		Refusal = Verdict(false, FString(PlanningPolicy::EditRejection(false, false)), ECommandRejection::Unavailable);
		return nullptr;
	}
	if (!Kit)
	{
		Refusal = Verdict(false, TEXT("Planning rejected: you have no kit."), ECommandRejection::InvalidOwner);
		return nullptr;
	}
	if (const TCHAR* Reason = PlanningPolicy::EditRejection(State->IsPlanning(), Kit->bReady))
	{
		Refusal = Verdict(false, Reason, ECommandRejection::Unavailable);
		return nullptr;
	}
	return Kit;
}

// The region the Barracks' force starts from: the Barracks' own, or the team main while it is unplaced.
int32 SourceRegion(const ACommandGameState& State, const FPlanningKit& Kit)
{
	const AMapRegion* Region = IsValid(Kit.Barracks) ? State.FindRegionAt(Kit.Barracks->GetActorLocation()) : nullptr;
	return Region ? Region->RegionIndex : ForceOrderGraph::TeamMain(State, 0);
}

bool ValidTarget(const ACommandGameState& State, const FPlanningKit& Kit, EForceVerb Verb, int32& RegionIndex,
	AActor* Structure, FString& OutReason)
{
	if (Verb != EForceVerb::MoveHold && Verb != EForceVerb::Attack)
	{
		OutReason = TEXT("First order rejected: Move & Hold or Attack only.");
		return false;
	}
	if (Structure
		&& (Verb != EForceVerb::Attack || !IsValid(Structure) || Structure->GetWorld() != State.GetWorld()
			|| Cast<AArmyUnit>(Structure) || !CombatTarget::IsAliveHostile(Structure, 0)))
	{
		OutReason = TEXT("Attack requires a living hostile structure.");
		return false;
	}
	if (Structure)
	{
		const AMapRegion* Region = State.FindRegionAt(Structure->GetActorLocation());
		RegionIndex = Region ? Region->RegionIndex : INDEX_NONE;
	}
	uint64 Graph[ForceOrders::MaxRegions];
	const int32 Count = ForceOrderGraph::ReadGraph(State, Graph);
	if (!ForceOrderGraph::Region(State, RegionIndex)
		|| ForceOrders::NextWaypoint(Graph, Count, SourceRegion(State, Kit), RegionIndex) == INDEX_NONE)
	{
		OutReason = TEXT("First order rejected: choose a reachable region.");
		return false;
	}
	return true;
}
}

FCommandResult FPlanningCommands::SetReady(ACommandPlayerState* Commander, bool bReady)
{
	ACommandGameState* State = PlanningState(Commander);
	FPlanningKit* Kit = State && State->IsPlanning() ? State->FindKit(Commander) : nullptr;
	if (!Kit)
		return Verdict(false, TEXT("Ready rejected: no planning phase or no kit."), ECommandRejection::Unavailable);
	const bool bChanged = Kit->bReady != bReady;
	Kit->bReady = bReady;
	State->ForceNetUpdate();
	// A repeated Ready or Not-ready is accepted but is not a decision. Recorded before the last Ready ends planning
	// and drops the kits.
	if (bChanged)
		State->MatchTelemetry->RecordAccepted(Commander, EMatchDecision::Order);
	State->EvaluatePlanningEnd();
	return Verdict(true, bReady ? TEXT("Ready: your kit is locked.") : TEXT("Not ready: your kit is editable."));
}

FCommandResult FPlanningCommands::PlaceKit(ACommandPlayerState* Commander, EBuildingKind Piece, const FVector& Location)
{
	FCommandResult Refusal;
	FPlanningKit* Kit = EditableKit(Commander, Refusal);
	if (!Kit)
		return Refusal;
	if (Piece != EBuildingKind::Barracks && Piece != EBuildingKind::Extractor)
		return Verdict(false, TEXT("Kit placement rejected: Barracks or Drill Rig only."));
	ACommandGameState* State = PlanningState(Commander);
	FString Reason;
	const bool bPlaced = State->PlaceKitPiece(*Kit, Piece == EBuildingKind::Extractor, Location, Reason);
	if (bPlaced)
		State->MatchTelemetry->RecordAccepted(Commander, EMatchDecision::Build);
	FCommandResult Result = Verdict(bPlaced, bPlaced ? FString(TEXT("Kit piece placed.")) : MoveTemp(Reason));
	Result.Building = Piece == EBuildingKind::Extractor ? Kit->Rig.Get() : Kit->Barracks.Get();
	return Result;
}

FCommandResult FPlanningCommands::SetUnitType(ACommandPlayerState* Commander, EUnitRole Role)
{
	FCommandResult Refusal;
	FPlanningKit* Kit = EditableKit(Commander, Refusal);
	if (!Kit)
		return Refusal;
	ACommandGameState* State = PlanningState(Commander);
	if (!IsValid(State->Content) || State->Content->UnitIndexForRole(Role) == INDEX_NONE)
		return Verdict(false, TEXT("Unit type rejected: no such unit."));
	Kit->UnitRole = Role;
	State->ForceNetUpdate();
	State->MatchTelemetry->RecordAccepted(Commander, EMatchDecision::Build);
	return Verdict(true, TEXT("Barracks unit type set."));
}

FCommandResult FPlanningCommands::SetFirstOrder(ACommandPlayerState* Commander, EForceVerb Verb, int32 RegionIndex,
	AActor* Structure, bool bQueue)
{
	FCommandResult Refusal;
	FPlanningKit* Kit = EditableKit(Commander, Refusal);
	if (!Kit)
		return Refusal;
	ACommandGameState* State = PlanningState(Commander);
	FString Reason;
	if (!ValidTarget(*State, *Kit, Verb, RegionIndex, Structure, Reason))
		return Verdict(false, MoveTemp(Reason));
	if (!ForceOrders::CanQueue(Kit->Orders.Num(), bQueue))
		return Verdict(false, TEXT("First order rejected: three orders maximum."));
	if (!bQueue)
		Kit->Orders.Reset();
	FPlanningOrder& Order = Kit->Orders.AddDefaulted_GetRef();
	Order.Verb = Verb;
	Order.RegionIndex = RegionIndex;
	Order.Structure = Structure;
	State->ForceNetUpdate();
	State->MatchTelemetry->RecordAccepted(Commander, EMatchDecision::Order);
	return Verdict(true, TEXT("First order set."));
}

FCommandResult FPlanningCommands::ClearFirstOrders(ACommandPlayerState* Commander)
{
	FCommandResult Refusal;
	FPlanningKit* Kit = EditableKit(Commander, Refusal);
	if (!Kit)
		return Refusal;
	Kit->Orders.Reset();
	PlanningState(Commander)->ForceNetUpdate();
	PlanningState(Commander)->MatchTelemetry->RecordAccepted(Commander, EMatchDecision::Order);
	return Verdict(true, TEXT("First orders cleared."));
}
