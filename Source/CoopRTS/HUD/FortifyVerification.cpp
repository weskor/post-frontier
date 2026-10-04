#include "FortifyVerification.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "CommandCamera.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/AbilityCommandComponent.h"
#include "Commands/CommandService.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "ForceBar.h"
#include "HUDPanels.h"
#include "Json.h"
#include "MapRegion.h"
#include "UnrealClient.h"

namespace FortifyVerification
{
using namespace CommandHUDPanels;
using namespace CoopRTSNetworkVerification::Probe;

static FString CheckLayout(ACommandPlayerController& Controller)
{
	int32 Width, Height;
	Controller.GetViewportSize(Width, Height);
	const FContext Context = MakeContext(&Controller);
	const FLayout Layout = MakeLayout(Context, Width, Height);
	const FRect& Dock = Layout.FortifyDock;
	if (Dock.W != 144.f || Dock.H != 42.f || Layout.Minimap.Y - Dock.Y != 66.f
		|| Dock.Intersects(Layout.Minimap) || Dock.Intersects(Layout.Build) || Dock.Intersects(Layout.Bottom))
		return TEXT("Fortify dock geometry overlaps the footer");
	bool bOverlap = false;
	ForEachForceCard(Context, Layout, [&](AArmyGroup*, const FRect& Rect) { bOverlap |= Rect.Intersects(Dock); });
	FJevIntentModel Model;
	BuildJevIntentModel(Context, Model);
	FJevMemoRow Rows[JevIntent::MemoVisible];
	const int32 Count = JevMemoRows(Context, Layout, Model, Rows);
	for (int32 Index = 0; Index < Count; ++Index)
		bOverlap |= Rows[Index].Rect.Bottom() > Dock.Y - Gap;
	return bOverlap ? TEXT("Fortify dock overlaps force cards or JEV memos") : FString();
}

static FString Aim(ACommandPlayerController& Controller, const AMapRegion& Region, const FJsonObject& Request)
{
	FVector2D Position;
	if (!Controller.ProjectWorldLocationToScreen(Controller.GetWorld()->GetGameState<ACommandGameState>()->GetRegionAnchor(Region.RegionIndex), Position))
		return TEXT("Fortify region cursor projection failed");
	FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	if (!Viewport || Position.X < 0.f || Position.Y < 0.f || Position.X >= Viewport->GetSizeXY().X || Position.Y >= Viewport->GetSizeXY().Y)
		return TEXT("Fortify region cursor outside viewport");
	Viewport->SetMouse(FMath::RoundToInt(Position.X), FMath::RoundToInt(Position.Y));
	const FFortifyPreview Preview = Controller.GetFortifyPreview(Position);
	if (Preview.RegionIndex != Region.RegionIndex)
		return TEXT("Fortify cursor resolver picked a different region");
	FString Expected;
	Request.TryGetStringField(TEXT("expected"), Expected);
	if ((Expected == TEXT("allowed") && (!Preview.IsAllowed() || Preview.Decision.bRefresh))
		|| (Expected == TEXT("refresh") && (!Preview.IsAllowed() || !Preview.Decision.bRefresh))
		|| (Expected == TEXT("rejected") && Preview.IsAllowed()))
		return TEXT("Fortify cursor verdict disagrees with capture state");
	return FString();
}

static FString TeammateCast(UWorld& World, ACommandGameState& State, ACommandPlayerController& Controller, AMapRegion& Region)
{
	ACommandPlayerState* Own = Controller.GetPlayerState<ACommandPlayerState>();
	ACommandPlayerState* Teammate = nullptr;
	for (APlayerState* Player : State.PlayerArray)
		if (ACommandPlayerState* Candidate = Cast<ACommandPlayerState>(Player); Candidate && Candidate != Own && Candidate->TeamIndex == Own->TeamIndex)
		{
			Teammate = Candidate;
			break;
		}
	if (!Teammate)
	{
		Teammate = World.SpawnActor<ACommandPlayerState>();
		if (!Teammate)
			return TEXT("Fortify teammate fixture spawn failed");
		Teammate->CommanderIndex = Own->CommanderIndex == 0 ? 1 : 0;
		Teammate->TeamIndex = Own->TeamIndex;
		Teammate->SetPlayerName(TEXT("Fixture teammate"));
		State.AddPlayerState(Teammate);
	}
	Teammate->Data = FortifyPolicy::DataCost;
	Teammate->FortifyReadyAt = 0.f;
	return FCommandService::CastFortify(Teammate, &Region).IsAccepted() ? FString() : TEXT("Fortify teammate authority cast rejected");
}

static FString FocusFeed(ACommandPlayerController& Controller)
{
	const FObjectiveEventView Events = Controller.AbilityCommands->GetEvents();
	if (Events.IsEmpty())
		return TEXT("Fortify team feed is empty");
	const FObjectiveEvent& Event = Events.Last();
	const ACommandHUD* HUD = Cast<ACommandHUD>(Controller.GetHUD());
	FVector2D Position;
	FVector World;
	int32 Sequence;
	if (!HUD || !HUD->FindAlertScreenPosition(Event.Sequence, Position)
		|| !HUD->GetAlertWorldPosition(Position, World, Sequence) || Sequence != Event.Sequence || !World.Equals(Event.Location))
		return TEXT("Fortify feed focus geometry disagrees");
	const int32 HistorySequence = Controller.GetFocusedAlertSequence();
	ACommandCamera* Camera = Cast<ACommandCamera>(Controller.GetPawn());
	if (!Camera || !Controller.HandleHUDClick(Position) || FVector::DistSquared2D(Camera->GetActorLocation(), Event.Location) > 1.f)
		return TEXT("Fortify feed click failed to focus its region");
	if (Controller.GetFocusedAlertSequence() != HistorySequence)
		return TEXT("Fortify feed click changed the Space history");
	return FString();
}

bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error)
{
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (!Action.StartsWith(TEXT("fortifyHud")))
		return false;
	ACommandPlayerController* Controller = LocalController(&World);
	ACommandGameState* State = World.GetGameState<ACommandGameState>();
	if (!Controller || !State)
	{
		Error = TEXT("Fortify HUD world unavailable");
		return true;
	}
	if (Action == TEXT("fortifyHudLayout"))
		Error = CheckLayout(*Controller);
	else if (Action == TEXT("fortifyHudFeedFocus"))
		Error = FocusFeed(*Controller);
	else
	{
		AMapRegion* Region = nullptr;
		for (AMapRegion* Candidate : State->Regions)
			if (IsValid(Candidate) && Candidate->RegionIndex == Request->GetIntegerField(TEXT("region")))
				Region = Candidate;
		if (!Region)
			Error = TEXT("Fortify HUD region unavailable");
		else if (Action == TEXT("fortifyHudAim"))
			Error = Aim(*Controller, *Region, *Request);
		else if (!bAuthorityFixtures || World.GetNetMode() != NM_ListenServer || !World.GetAuthGameMode())
			Error = TEXT("Fortify HUD fixture requires opted-in authority host");
		else if (Action == TEXT("fortifyHudFocus"))
		{
			if (ACommandCamera* Camera = Cast<ACommandCamera>(Controller->GetPawn()))
				Camera->FocusOn(State->GetRegionAnchor(Region->RegionIndex));
			else
				Error = TEXT("Fortify camera unavailable");
		}
		else if (Action == TEXT("fortifyHudRemaining") && Region->IsFortifyActive())
			Region->FortifyExpiresAt = State->GetServerWorldTimeSeconds() + Request->GetNumberField(TEXT("seconds"));
		else if (Action == TEXT("fortifyHudTeammate"))
			Error = TeammateCast(World, *State, *Controller, *Region);
		else if (Action == TEXT("fortifyHudLost"))
			Region->EndFortify(FortifyPolicy::EEnd::RegionLost);
		else
			Error = TEXT("unknown Fortify HUD action or inactive fixture");
	}
	return true;
}
}
#endif
