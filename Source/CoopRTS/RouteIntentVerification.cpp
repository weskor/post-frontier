#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "RouteIntentVerification.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Commands/OrderGraph.h"
#include "Engine/World.h"
#include "Json.h"

namespace RouteIntentVerification
{
namespace
{
TWeakObjectPtr<AArmyGroup> Teammate;
TWeakObjectPtr<ACommandPlayerState> Commander;
}
void Snapshot(const AArmyGroup& Force, const TSharedPtr<FJsonObject>& Result)
{
	TArray<TSharedPtr<FJsonValue>> Routes;
	for (const FForceRoute& Route : Force.GetIntentRoutes())
	{
		auto Entry = MakeShared<FJsonObject>();
		Entry->SetNumberField(TEXT("orderIndex"), Route.OrderIndex);
		TArray<TSharedPtr<FJsonValue>> Regions;
		for (int32 Region : Route.Regions)
			Regions.Add(MakeShared<FJsonValueNumber>(Region));
		Entry->SetArrayField(TEXT("regions"), Regions);
		Routes.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("intentRoutes"), Routes);
}
void HoverSnapshot(const ACommandPlayerController& Controller, const TSharedPtr<FJsonObject>& Result)
{
	FForceOrder Hover;
	bool bQueue = false;
	const bool bValid = Controller.GetHoveredForceOrder(Hover, bQueue);
	Result->SetBoolField(TEXT("routePreview"), bValid);
	Result->SetNumberField(TEXT("previewRegion"), bValid ? Hover.RegionIndex : INDEX_NONE);
	Result->SetNumberField(TEXT("previewVerb"), bValid ? static_cast<int32>(Hover.Verb) : -1);
	Result->SetBoolField(TEXT("previewQueue"), bQueue);
}
FString TeammateFixture(ACommandPlayerController& Controller, int32 Target, bool bEnable)
{
	if (Teammate.IsValid())
		Teammate->Destroy();
	if (Commander.IsValid())
		Commander->Destroy();
	if (!bEnable)
		return FString();
	UWorld* World = Controller.GetWorld();
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (!Controller.HasAuthority() || !State || Controller.GetSelectedForces().IsEmpty()
		|| !ForceOrderGraph::Region(*State, Target))
		return TEXT("route fixture requires selected force and valid target");
	Commander = World->SpawnActor<ACommandPlayerState>();
	if (!Commander.IsValid())
		return TEXT("route teammate commander allocation failed");
	Commander->TeamIndex = 0;
	Commander->CommanderIndex = 1;
	Commander->SetPlayerName(TEXT("Route teammate"));
	const FVector Centre = Controller.GetSelectedForces()[0]->GetCenter() + FVector(0., 650., 0.);
	const FTransform Transform(Centre);
	Teammate = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Teammate.IsValid())
		return TEXT("route teammate force allocation failed");
	Teammate->Initialize({ 0, Commander.Get(), 0, nullptr, Centre });
	Teammate->ForceNumber = 1;
	Teammate->FinishSpawning(Transform);
	if (!Teammate->SpawnUnits())
		return TEXT("route teammate member spawn failed");
	const FCommandResult Result = FCommandService::IssueForceOrder(Commander.Get(), Teammate.Get(), EForceVerb::MoveHold, Target);
	return Result.IsAccepted() ? FString() : Result.Message;
}
}
#endif
