// Probe snapshot of the local world: session, match, wallets and the local controller's UI state.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/MatchCommandComponent.h"
#include "EnemyCommander.h"
#include "EngineUtils.h"
#include "HUD/OrderInputPreview.h"
#include "HUD/TeamPanelVerification.h"
#include "Json.h"
#include "GameFramework/Pawn.h"
#include "Engine/NetDriver.h"
#include "RouteIntentVerification.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
void HudButtonsSnapshot(const ACommandHUD& HUD, const TSharedPtr<FJsonObject>& Result)
{
	TArray<TSharedPtr<FJsonValue>> Buttons;
	for (int32 Index = 1; Index <= static_cast<int32>(EHUDAction::ForceCard60); ++Index)
	{
		FVector2D Position;
		if (!HUD.FindActionScreenPosition(static_cast<EHUDAction>(Index), Position))
			continue;
		auto Button = Object();
		Number(Button, TEXT("action"), Index);
		Number(Button, TEXT("x"), Position.X);
		Number(Button, TEXT("y"), Position.Y);
		Buttons.Add(MakeShared<FJsonValueObject>(Button));
	}
	Result->SetArrayField(TEXT("uiButtons"), Buttons);
}

void ControllerUiSnapshot(const ACommandPlayerController& PC, const TSharedPtr<FJsonObject>& Result)
{
	Number(Result, TEXT("uiScreen"), static_cast<int32>(PC.GetUIScreen()));
	Number(Result, TEXT("masterVolume"), PC.GetMasterVolume());
	Result->SetBoolField(TEXT("menuWorld"), PC.IsMenuWorld());
	Number(Result, TEXT("pauseFeedbackSerial"), PC.MatchCommands->PauseFeedbackSerial);
	Result->SetBoolField(TEXT("pauseAccepted"), PC.MatchCommands->bLastPauseAccepted);
	if (const ACommandHUD* HUD = Cast<ACommandHUD>(PC.GetHUD()))
		HudButtonsSnapshot(*HUD, Result);
}

void NetDriverSnapshot(UWorld* World, const TSharedPtr<FJsonObject>& Result)
{
	if (const UNetDriver* Driver = World->GetNetDriver())
	{
		Number(Result, TEXT("netDriverId"), LifetimeId(Driver));
#if DO_ENABLE_NET_TEST
		Number(Result, TEXT("pktLag"), Driver->PacketSimulationSettings.PktLag);
		Number(Result, TEXT("pktLoss"), Driver->PacketSimulationSettings.PktLoss);
#endif
	}
}

void MatchSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	Number(Result, TEXT("result"), static_cast<int32>(State.MatchResult));
	Result->SetBoolField(TEXT("activePaused"), State.IsActivePaused());
	Result->SetBoolField(TEXT("coopPauseSpent"), State.IsCoopPauseSpent());
	Number(Result, TEXT("pauseRemaining"), State.GetPauseSecondsRemaining());
	Result->SetBoolField(TEXT("placementValid"), bPlacementCandidateValid);
	Vector(Result, TEXT("placementCandidate"), PlacementCandidate);
	Number(Result, TEXT("enemyResources"), IsValid(State.EnemyCommander) ? State.EnemyCommander->Resources : 0);
	if (IsValid(State.Arena))
		Result->SetArrayField(TEXT("arenaHalfExtent"), { MakeShared<FJsonValueNumber>(State.Arena->HalfExtent.X), MakeShared<FJsonValueNumber>(State.Arena->HalfExtent.Y) });
	Number(Result, TEXT("enemyIncome"), State.GetEnemyIncomePerSecond());
	Result->SetBoolField(TEXT("incomePaused"), State.bVerificationIncomePaused);
}

void PlayersSnapshot(const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Wallets = TArray<TSharedPtr<FJsonValue>>();
	for (const APlayerState* PS : State.PlayerArray)
	{
		const auto* Wallet = Cast<ACommandPlayerState>(PS);
		if (!IsValid(Wallet))
			continue;
		auto Entry = Object();
		Number(Entry, TEXT("index"), Wallet->CommanderIndex);
		Number(Entry, TEXT("team"), Wallet->TeamIndex);
		Entry->SetStringField(TEXT("playerName"), Wallet->GetPlayerName());
		Number(Entry, TEXT("wallet"), Wallet->Resources);
		Number(Entry, TEXT("income"), State.GetIncomePerSecond(Wallet));
		Number(Entry, TEXT("doctrine"), static_cast<int32>(Wallet->Doctrine));
		Wallets.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("players"), Wallets);
}

void LocalSelectionSnapshot(ACommandPlayerController& PC, const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	Result->SetStringField(TEXT("orderFeedback"), PC.GetOrderFeedback());
	Number(Result, TEXT("feedbackOpacity"), PC.GetFeedbackOpacity());
	Result->SetBoolField(TEXT("hudExpanded"), PC.IsHUDExpanded());
	const ACommandHUD* DeckHUD = Cast<ACommandHUD>(PC.GetHUD());
	Result->SetBoolField(TEXT("deckOpen"), DeckHUD && DeckHUD->IsDeckOpen());
	Result->SetBoolField(TEXT("placing"), PC.IsPlacingBuilding());
	Result->SetBoolField(TEXT("assigningOrder"), PC.IsAssigningOrder());
	Number(Result, TEXT("pendingVerb"), static_cast<int32>(PC.GetPendingVerb()));
	RouteIntentVerification::HoverSnapshot(PC, Result);
	Result->SetBoolField(TEXT("buildingSelected"), IsValid(PC.GetSelectedBuilding()));
	Number(Result, TEXT("selectedBuilding"), State.Buildings.IndexOfByKey(PC.GetSelectedBuilding()));
	TArray<TSharedPtr<FJsonValue>> Selected;
	for (const AArmyGroup* Force : PC.GetSelectedForces())
		if (IsValid(Force))
			Selected.Add(MakeShared<FJsonValueNumber>(LifetimeId(Force)));
	Result->SetArrayField(TEXT("selectedForces"), Selected);
	Number(Result, TEXT("inspectedForce"), IsValid(PC.GetInspectedForce()) ? LifetimeId(PC.GetInspectedForce()) : -1);
	FVector2D DragStart, DragEnd;
	Result->SetBoolField(TEXT("selectionDragging"), PC.GetSelectionDrag(DragStart, DragEnd));
	if (const APawn* Camera = PC.GetPawn())
		Vector(Result, TEXT("cameraPosition"), Camera->GetActorLocation());
	if (const ACommandHUD* HUD = Cast<ACommandHUD>(PC.GetHUD()))
	{
		FVector2D Origin;
		float Size;
		if (HUD->GetMinimapScreenRect(Origin, Size))
		{
			Result->SetArrayField(TEXT("minimapOrigin"), { MakeShared<FJsonValueNumber>(Origin.X), MakeShared<FJsonValueNumber>(Origin.Y) });
			Number(Result, TEXT("minimapSize"), Size);
		}
	}
}

void LocalViewportSnapshot(ACommandPlayerController& PC, const TSharedPtr<FJsonObject>& Result)
{
	const ACommandHUD* DeckHUD = Cast<ACommandHUD>(PC.GetHUD());
	int32 ViewportWidth = 0, ViewportHeight = 0;
	PC.GetViewportSize(ViewportWidth, ViewportHeight);
	Number(Result, TEXT("viewportWidth"), ViewportWidth);
	Number(Result, TEXT("viewportHeight"), ViewportHeight);
	Result->SetBoolField(TEXT("centreClear"), DeckHUD && !DeckHUD->IsPanelPoint(FVector2D(ViewportWidth, ViewportHeight) * .5f));
}

void LocalCursorSnapshot(ACommandPlayerController& PC, const TSharedPtr<FJsonObject>& Result)
{
	float CursorX, CursorY;
	if (!PC.GetMousePosition(CursorX, CursorY))
		return;
	const FVector2D Cursor(CursorX, CursorY);
	Result->SetArrayField(TEXT("cursorScreen"), { MakeShared<FJsonValueNumber>(Cursor.X), MakeShared<FJsonValueNumber>(Cursor.Y) });
	const FOrderInputPreview Preview = PC.GetOrderPreview(Cursor,
		PC.IsInputKeyDown(EKeys::LeftShift) || PC.IsInputKeyDown(EKeys::RightShift));
	auto PreviewEntry = Object();
	Number(PreviewEntry, TEXT("resolution"), static_cast<int32>(Preview.Resolution));
	Number(PreviewEntry, TEXT("rejection"), static_cast<int32>(Preview.Rejection));
	Number(PreviewEntry, TEXT("regionIndex"), Preview.RegionIndex);
	Number(PreviewEntry, TEXT("structureId"), IsValid(Preview.Structure) ? LifetimeId(Preview.Structure) : -1);
	PreviewEntry->SetBoolField(TEXT("allowed"), Preview.IsAllowed());
	PreviewEntry->SetStringField(TEXT("label"), Preview.Label());
	Result->SetObjectField(TEXT("orderPreview"), PreviewEntry);
	FVector Origin, Direction;
	if (PC.DeprojectScreenPositionToWorld(Cursor.X, Cursor.Y, Origin, Direction)
		&& FMath::Abs(Direction.Z) >= KINDA_SMALL_NUMBER)
	{
		const double Time = -Origin.Z / Direction.Z;
		if (Time >= 0.f)
			Vector(Result, TEXT("cursorWorld"), Origin + Direction * Time);
	}
}

// Fills the first local controller's fields and returns its commander index, or -1 without one.
int32 LocalControllerSnapshot(UWorld* World, const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
		if (It->IsLocalController())
		{
			LocalSelectionSnapshot(**It, State, Result);
			LocalViewportSnapshot(**It, Result);
			LocalCursorSnapshot(**It, Result);
			if (const auto* PS = It->GetPlayerState<ACommandPlayerState>())
			{
				Number(Result, TEXT("income"), State.GetIncomePerSecond(PS));
				return PS->CommanderIndex;
			}
			return -1;
		}
	return -1;
}
}

TSharedPtr<FJsonObject> Snapshot(UWorld* World)
{
	auto Result = Object();
	Number(Result, TEXT("generation"), Generation);
	Result->SetBoolField(TEXT("ready"), World != nullptr);
	if (!World)
		return Result;
	Number(Result, TEXT("netMode"), static_cast<int32>(World->GetNetMode()));
	Result->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(World->GetOutermost()->GetName()));
	Result->SetBoolField(TEXT("worldPaused"), World->IsPaused());
	Number(Result, TEXT("worldTime"), World->GetTimeSeconds());
	if (const ACommandPlayerController* PC = LocalController(World))
		ControllerUiSnapshot(*PC, Result);
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (!State)
	{
		Result->SetBoolField(TEXT("ready"), false);
		return Result;
	}
	Number(Result, TEXT("gameStateId"), LifetimeId(State));
	NetDriverSnapshot(World, Result);
	MatchSnapshot(*State, Result);
	PlayersSnapshot(*State, Result);
	Number(Result, TEXT("localIndex"), LocalControllerSnapshot(World, *State, Result));
	ArmiesSnapshot(World, *State, Result);
	StructuresSnapshot(*State, Result);
	ObjectiveSnapshot(State, Result);
	PingSnapshot(World, Result);
	JevIntentSnapshot(World, Result);
	AbilitySnapshot(World, *State, Result);
	PlanningSnapshot(*State, Result);
	TeamPanelVerification::Snapshot(*World, *State, Result);
	return Result;
}
}
#endif
