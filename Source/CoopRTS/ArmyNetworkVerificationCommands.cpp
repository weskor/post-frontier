// Probe actions for the ownership, production and order scenarios. Commands go through the owning controller's RPCs.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/ConstructionCommandComponent.h"
#include "Commands/OrderCommandComponent.h"
#include "Commands/ProductionCommandComponent.h"
#include "ForceOrders.h"
#include "Headquarters.h"
#include "Json.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
FString CommandForceOrderRpc(const FProbeRequest& Probe, ACommandBuilding& Building)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	if (!IsValid(Building.ForceGroup))
		return TEXT("force not replicated locally");
	Probe.PC->OrderCommands->ServerIssueForceOrder({ Building.ForceGroup },
		static_cast<EForceVerb>(Request->GetIntegerField(TEXT("forceVerb"))),
		static_cast<int32>(Request->GetIntegerField(TEXT("targetRegionIndex"))),
		Request->GetBoolField(TEXT("targetEnemyHQ")) ? Probe.State->EnemyHeadquarters.Get() : nullptr,
		Request->GetBoolField(TEXT("queue")), 0);
	return FString();
}

FString CommandBuildingRpc(const FProbeRequest& Probe)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	ACommandPlayerController* PC = Probe.PC;
	const ACommandGameState* State = Probe.State;
	const int32 BuildingIndex = Request->GetIntegerField(TEXT("building"));
	if (!State->Buildings.IsValidIndex(BuildingIndex) || !IsValid(State->Buildings[BuildingIndex]))
		return TEXT("building not replicated locally");
	ACommandBuilding* Building = State->Buildings[BuildingIndex];
	if (Probe.Action == TEXT("production"))
		PC->ProductionCommands->ServerConfigureProduction(Building,
			static_cast<EUnitRole>(Request->GetIntegerField(TEXT("recipe"))), Request->GetBoolField(TEXT("enabled")));
	// Bypass local selection only for deliberately invalid/foreign RPC validation.
	else if (Probe.Action == TEXT("forceOrderRPC"))
		return CommandForceOrderRpc(Probe, *Building);
	else if (Probe.Action == TEXT("cancel"))
		PC->ConstructionCommands->ServerCancelBuilding(Building);
	else
		PC->ProductionCommands->ServerResearch(Building, static_cast<EArmyDoctrine>(Request->GetIntegerField(TEXT("choice"))));
	return FString();
}

FString CommandConstructionRpc(const FProbeRequest& Probe)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	if (!Probe.Own || Probe.Own->CommanderIndex < 0 || !Probe.State)
		return TEXT("local owning controller unavailable");
	if (Probe.Action == TEXT("build"))
	{
		Probe.PC->ConstructionCommands->ServerPlaceBuilding(static_cast<int32>(Request->GetIntegerField(TEXT("kind"))),
			FVector(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")), 5.f));
		return FString();
	}
	return CommandBuildingRpc(Probe);
}

FString CommandPlace(const FProbeRequest& Probe)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	ACommandPlayerController* PC = Probe.PC;
	if (!PC || !PC->IsPlacingBuilding())
		return TEXT("placement mode unavailable");
	PC->PlaceBuildingAt(FVector(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")), 5.f),
		PC->IsInputKeyDown(EKeys::LeftShift) || PC->IsInputKeyDown(EKeys::RightShift));
	return FString();
}

FString CommandOrderInput(const FProbeRequest& Probe)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	ACommandPlayerController* PC = Probe.PC;
	if (!PC)
		return TEXT("local order controller unavailable");
	bool bQueue = false;
	Request->TryGetBoolField(TEXT("queue"), bQueue);
	if (Probe.Action == TEXT("beginAttack"))
		PC->BeginForceAttack();
	else if (Probe.Action == TEXT("retreat"))
		PC->RetreatSelectedForces(bQueue);
	else
	{
		const FVector2D Position(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")));
		if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y))
			return TEXT("invalid order screen point");
		if (Probe.Action == TEXT("orderClick"))
			PC->HandleOrderClick(Position, bQueue);
		else
			PC->ConfirmAttackAtScreenPosition(Position, bQueue);
	}
	// A resolved rejection is gameplay feedback, not a broken probe request.
	return FString();
}
}

bool HandleCommandAction(const FProbeRequest& Probe, FString& Error)
{
	const FString& Action = Probe.Action;
	if (Action == TEXT("build") || Action == TEXT("production") || Action == TEXT("forceOrderRPC")
		|| Action == TEXT("cancel") || Action == TEXT("research"))
		Error = CommandConstructionRpc(Probe);
	else if (Action == TEXT("place"))
		Error = CommandPlace(Probe);
	else if (Action == TEXT("orderClick") || Action == TEXT("beginAttack")
		|| Action == TEXT("confirmAttack") || Action == TEXT("retreat"))
		Error = CommandOrderInput(Probe);
	else
		return false;
	return true;
}
}
#endif
