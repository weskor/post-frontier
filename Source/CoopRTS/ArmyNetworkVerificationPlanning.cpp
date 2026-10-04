// Probe snapshot and fixtures for the planning scenario. The host can issue FPlanningCommands for any commander slot
// (an authority fixture), and any peer can plan as its own commander through the controller's UPlanningCommandComponent
// (planningClient*); every peer reports what the replicated Planning state shows.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/PlanningCommandComponent.h"
#include "Commands/PlanningCommands.h"
#include "DepositSite.h"
#include "Headquarters.h"
#include "Json.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
TSharedPtr<FJsonValue> PieceJson(const ACommandBuilding* Piece)
{
	if (!IsValid(Piece))
		return MakeShared<FJsonValueNull>();
	auto Entry = Object();
	Vector(Entry, TEXT("location"), Piece->GetActorLocation());
	Entry->SetBoolField(TEXT("complete"), Piece->IsComplete());
	Entry->SetBoolField(TEXT("configured"), Piece->bForceConfigured);
	Number(Entry, TEXT("role"), static_cast<int32>(Piece->ProductionRole));
	return MakeShared<FJsonValueObject>(Entry);
}

TSharedPtr<FJsonObject> KitJson(const FPlanningKit& Kit)
{
	auto Entry = Object();
	Number(Entry, TEXT("slot"), IsValid(Kit.Commander) ? Kit.Commander->CommanderIndex : -1);
	Number(Entry, TEXT("power"), IsValid(Kit.Commander) ? Kit.Commander->Resources : -1);
	Entry->SetBoolField(TEXT("ready"), Kit.bReady);
	Number(Entry, TEXT("role"), static_cast<int32>(Kit.UnitRole));
	Entry->SetField(TEXT("barracks"), PieceJson(Kit.Barracks));
	Entry->SetField(TEXT("rig"), PieceJson(Kit.Rig));
	TArray<TSharedPtr<FJsonValue>> Orders;
	for (const FPlanningOrder& Order : Kit.Orders)
	{
		auto Row = Object();
		Number(Row, TEXT("verb"), static_cast<int32>(Order.Verb));
		Number(Row, TEXT("region"), Order.RegionIndex);
		Orders.Add(MakeShared<FJsonValueObject>(Row));
	}
	Entry->SetArrayField(TEXT("orders"), Orders);
	return Entry;
}

ACommandPlayerState* CommanderAt(const ACommandGameState& State, int32 Slot)
{
	for (APlayerState* Player : State.PlayerArray)
		if (auto* Candidate = Cast<ACommandPlayerState>(Player); Candidate && Candidate->CommanderIndex == Slot)
			return Candidate;
	return nullptr;
}

// The first legal Barracks spot clear of every deposit, searched in rings around the friendly headquarters.
bool FindBarracksSpot(const ACommandGameState& State, FVector& OutLocation)
{
	const FVector Center = State.FriendlyHeadquarters->GetActorLocation();
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			const FVector Point = State.ResolveBuildingLocation(0,
				Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f));
			bool bClear = true;
			for (const ADepositSite* Deposit : State.Deposits)
				bClear &= !IsValid(Deposit) || FVector::Dist2D(Point, Deposit->GetActorLocation()) >= 400.;
			FString Reason;
			if (bClear && State.ValidateBuildingPlacement(0, 0, Point, Reason))
			{
				OutLocation = Point;
				return true;
			}
		}
	return false;
}

FString PlacePiece(const FProbeRequest& Probe, ACommandPlayerState& Commander)
{
	const bool bRig = Probe.Request->GetStringField(TEXT("piece")) == TEXT("rig");
	FVector Spot;
	if (!bRig)
		return FindBarracksSpot(*Probe.State, Spot) && FPlanningCommands::PlaceKit(&Commander, EBuildingKind::Barracks, Spot).IsAccepted()
			? FString()
			: FString(TEXT("kit Barracks could not be placed"));
	for (const ADepositSite* Deposit : Probe.State->Deposits)
		if (IsValid(Deposit) && !IsValid(Deposit->Extractor) && Probe.State->GetRegionController(Deposit->RegionIndex) == 0
			&& FPlanningCommands::PlaceKit(&Commander, EBuildingKind::Extractor, Deposit->GetActorLocation()).IsAccepted())
			return FString();
	return TEXT("kit Drill Rig could not be placed");
}
}

void PlanningSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Planning = Object();
	Planning->SetBoolField(TEXT("active"), State.IsPlanning());
	Number(Planning, TEXT("remaining"), State.Planning.SecondsRemaining);
	Number(Planning, TEXT("jevKits"), State.Planning.JevKits.Num());
	Number(Planning, TEXT("clockStart"), State.GetBattleClockStartServerTime());
	TArray<TSharedPtr<FJsonValue>> Kits;
	for (const FPlanningKit& Kit : State.Planning.Kits)
		Kits.Add(MakeShared<FJsonValueObject>(KitJson(Kit)));
	Planning->SetArrayField(TEXT("kits"), Kits);
	Result->SetObjectField(TEXT("planning"), Planning);
}

bool HandlePlanningFixture(const FProbeRequest& Probe, FString& Error)
{
	if (!Probe.Action.StartsWith(TEXT("planning")))
		return false;
	ACommandPlayerState* Commander = CommanderAt(*Probe.State, Probe.Owner);
	if (!Commander)
	{
		Error = TEXT("planning commander unavailable");
		return true;
	}
	FCommandResult Result;
	if (Probe.Action == TEXT("planningPlace"))
	{
		Error = PlacePiece(Probe, *Commander);
		return true;
	}
	if (Probe.Action == TEXT("planningType"))
		Result = FPlanningCommands::SetUnitType(Commander, static_cast<EUnitRole>(Probe.Request->GetIntegerField(TEXT("role"))));
	else if (Probe.Action == TEXT("planningReady"))
		Result = FPlanningCommands::SetReady(Commander, Probe.Request->GetBoolField(TEXT("ready")));
	else if (Probe.Action == TEXT("planningOrder"))
		Result = FPlanningCommands::SetFirstOrder(Commander, EForceVerb::MoveHold, static_cast<int32>(Probe.Request->GetIntegerField(TEXT("region"))));
	else
	{
		Error = TEXT("unknown planning action");
		return true;
	}
	Error = Result.IsAccepted() ? FString() : Result.Message;
	return true;
}

namespace
{
// The first deposit the local commander's team holds, free and uncontested, as the replicated world shows it.
bool FindFreeOwnDeposit(const ACommandGameState& State, FVector& OutLocation)
{
	for (const ADepositSite* Deposit : State.Deposits)
		if (IsValid(Deposit) && !IsValid(Deposit->Extractor) && Deposit->Remaining > 0 && State.GetRegionController(Deposit->RegionIndex) == 0
			&& !State.IsRegionContested(Deposit->RegionIndex, 0))
		{
			OutLocation = Deposit->GetActorLocation();
			return true;
		}
	return false;
}

FString ClientPlace(const FProbeRequest& Probe)
{
	FVector Spot;
	if (Probe.Request->GetStringField(TEXT("piece")) == TEXT("rig"))
	{
		if (!FindFreeOwnDeposit(*Probe.State, Spot))
			return TEXT("no free own deposit is replicated locally");
		Probe.PC->PlanningCommands->ServerPlaceKit(EBuildingKind::Extractor, Spot);
		return FString();
	}
	if (!FindBarracksSpot(*Probe.State, Spot))
		return TEXT("no legal Barracks spot is replicated locally");
	Probe.PC->PlanningCommands->ServerPlaceKit(EBuildingKind::Barracks, Spot);
	return FString();
}
}

// The local commander plans through its owning controller's RPC component, as the KIT bar, the chips and Enter do:
// planningClientPlace (piece), planningClientType (role), planningClientOrder (region), planningClientReady (ready).
// The verdict is not returned; the scenario reads the replicated Planning state.
bool HandlePlanningClient(const FProbeRequest& Probe, FString& Error)
{
	if (!Probe.Action.StartsWith(TEXT("planningClient")))
		return false;
	if (!Probe.PC || !Probe.State || !Probe.Own)
	{
		Error = TEXT("planning client controller unavailable");
		return true;
	}
	UPlanningCommandComponent& Commands = *Probe.PC->PlanningCommands;
	if (Probe.Action == TEXT("planningClientPlace"))
		Error = ClientPlace(Probe);
	else if (Probe.Action == TEXT("planningClientType"))
		Commands.ServerSetUnitType(static_cast<EUnitRole>(Probe.Request->GetIntegerField(TEXT("role"))));
	else if (Probe.Action == TEXT("planningClientOrder"))
		Commands.ServerSetFirstOrder(EForceVerb::MoveHold, static_cast<int32>(Probe.Request->GetIntegerField(TEXT("region"))), nullptr, false);
	else if (Probe.Action == TEXT("planningClientReady"))
		Commands.ServerSetReady(Probe.Request->GetBoolField(TEXT("ready")));
	else
		Error = TEXT("unknown planning client action");
	return true;
}
}
#endif
