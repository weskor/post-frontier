// Development-only, explicit command-line opt-in. Each process observes its own game world;
// only the listen host with the separate Authority switch can arrange encounters.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandHUD.h"
#include "Content/MatchContent.h"
#include "GameFramework/Pawn.h"
#include "CommandPlayerState.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "HAL/PlatformFileManager.h"
#include "Json.h"
#include "Misc/CommandLine.h"
#include "Misc/FileHelper.h"
#include "Misc/Parse.h"
#include "Containers/Ticker.h"
#include "UObject/UObjectArray.h"
#include "UnrealClient.h"

namespace CoopRTSNetworkVerification
{
namespace
{
FString Directory;
FString Peer;
bool bAuthorityFixtures = false;
FTSTicker::FDelegateHandle TickHandle;
int32 LastCommand = 0;
int32 Generation = 0;
TWeakObjectPtr<UWorld> LastWorld;
	FVector PlacementCandidate = FVector::ZeroVector;
	bool bPlacementCandidateValid = false;

TSharedPtr<FJsonObject> Object() { return MakeShared<FJsonObject>(); }
int32 LifetimeId(const UObject* Value)
{
	// Object indices can be reused after travel; weak-object serials distinguish lifetimes.
	return GUObjectArray.AllocateSerialNumber(Value->GetUniqueID());
}
void Number(const TSharedPtr<FJsonObject>& ObjectValue, const TCHAR* Key, double Value)
{
	ObjectValue->SetNumberField(Key, Value);
}
void Vector(const TSharedPtr<FJsonObject>& ObjectValue, const TCHAR* Key, FVector Value)
{
	TArray<TSharedPtr<FJsonValue>> Coordinates;
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.X));
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.Y));
	Coordinates.Add(MakeShared<FJsonValueNumber>(Value.Z));
	ObjectValue->SetArrayField(Key, Coordinates);
}
// Unit definition currently selected by a producer; production rules read the same definition.
const UArmyUnitDefinition* ProductionDefinition(const ACommandGameState& State, const ACommandBuilding& Building)
{
	if (!State.Content) return nullptr;
	for (int32 Index = 0; const UArmyUnitDefinition* Definition = State.Content->Unit(Index); ++Index)
		if (Definition->Role == Building.ProductionRole) return Definition;
	return nullptr;
}
TSharedPtr<FJsonObject> Snapshot(UWorld* World)
{
	auto Result = Object();
	Number(Result, TEXT("generation"), Generation);
	Result->SetBoolField(TEXT("ready"), World != nullptr);
	if (!World) return Result;
	Number(Result, TEXT("netMode"), static_cast<int32>(World->GetNetMode()));
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (!State) { Result->SetBoolField(TEXT("ready"), false); return Result; }
	Number(Result, TEXT("gameStateId"), LifetimeId(State));
	if (const UNetDriver* Driver = World->GetNetDriver())
	{
		Number(Result, TEXT("netDriverId"), LifetimeId(Driver));
#if DO_ENABLE_NET_TEST
		Number(Result, TEXT("pktLag"), Driver->PacketSimulationSettings.PktLag);
		Number(Result, TEXT("pktLoss"), Driver->PacketSimulationSettings.PktLoss);
#endif
	}
	Number(Result, TEXT("result"), static_cast<int32>(State->MatchResult));
	Result->SetBoolField(TEXT("placementValid"), bPlacementCandidateValid);
	Vector(Result, TEXT("placementCandidate"), PlacementCandidate);
	Number(Result, TEXT("enemyResources"), IsValid(State->EnemyCommander) ? State->EnemyCommander->Resources : 0);
	if (IsValid(State->Arena))
		Result->SetArrayField(TEXT("arenaHalfExtent"), {MakeShared<FJsonValueNumber>(State->Arena->HalfExtent.X),
			MakeShared<FJsonValueNumber>(State->Arena->HalfExtent.Y)});
	Number(Result, TEXT("resourceSites"), State->ControlledResourceSites);
	Result->SetBoolField(TEXT("incomePaused"), State->bVerificationIncomePaused);
	auto Wallets = TArray<TSharedPtr<FJsonValue>>();
	for (const APlayerState* PS : State->PlayerArray)
	{
		const auto* Wallet = Cast<ACommandPlayerState>(PS);
		if (!IsValid(Wallet)) continue;
		auto Entry = Object();
		Number(Entry, TEXT("index"), Wallet->CommanderIndex);
		Number(Entry, TEXT("wallet"), Wallet->Resources);
		Number(Entry, TEXT("doctrine"), static_cast<int32>(Wallet->Doctrine));
		Wallets.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("players"), Wallets);
	int32 LocalIndex = -1;
	for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
		if (It->IsLocalController())
		{
			Result->SetStringField(TEXT("orderFeedback"), It->GetOrderFeedback());
			Result->SetBoolField(TEXT("hudExpanded"), It->IsHUDExpanded());
			Result->SetBoolField(TEXT("placing"), It->IsPlacingBuilding());
			Result->SetBoolField(TEXT("assigningFront"), It->IsAssigningFront());
			Result->SetBoolField(TEXT("buildingSelected"), IsValid(It->GetSelectedBuilding()));
			Number(Result, TEXT("selectedBuilding"), State->Buildings.IndexOfByKey(It->GetSelectedBuilding()));
			if (const APawn* Camera = It->GetPawn()) Vector(Result, TEXT("cameraPosition"), Camera->GetActorLocation());
			if (const ACommandHUD* HUD = Cast<ACommandHUD>(It->GetHUD()))
			{
				FVector2D Origin;
				float Size;
				if (HUD->GetMinimapScreenRect(Origin, Size))
				{
					Result->SetArrayField(TEXT("minimapOrigin"), {MakeShared<FJsonValueNumber>(Origin.X), MakeShared<FJsonValueNumber>(Origin.Y)});
					Number(Result, TEXT("minimapSize"), Size);
				}
			}
			int32 ViewportWidth = 0, ViewportHeight = 0;
			It->GetViewportSize(ViewportWidth, ViewportHeight);
			Number(Result, TEXT("viewportWidth"), ViewportWidth);
			Number(Result, TEXT("viewportHeight"), ViewportHeight);
			if (const auto* PS = It->GetPlayerState<ACommandPlayerState>())
				LocalIndex = PS->CommanderIndex;
			break;
		}
	Number(Result, TEXT("localIndex"), LocalIndex);
	auto Armies = TArray<TSharedPtr<FJsonValue>>();
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
	{
		const AArmyGroup* Group = *It;
		auto Entry = Object();
		Number(Entry, TEXT("actorId"), LifetimeId(Group));
		Number(Entry, TEXT("owner"), IsValid(Group->OwningPlayerState) ? Group->OwningPlayerState->CommanderIndex : -1);
		Number(Entry, TEXT("team"), Group->TeamIndex);
		Number(Entry, TEXT("army"), Group->ArmyIndex);
		Number(Entry, TEXT("order"), static_cast<int32>(Group->Order));
		Number(Entry, TEXT("serial"), Group->OrderSerial);
		Number(Entry, TEXT("doctrine"), static_cast<int32>(Group->GetDoctrine()));
		Number(Entry, TEXT("frontOrder"), static_cast<int32>(Group->FrontOrder));
		Number(Entry, TEXT("producer"), IsValid(Group->ProductionBuilding) ? State->Buildings.IndexOfByKey(Group->ProductionBuilding) : -1);
		Entry->SetBoolField(TEXT("automaticFront"), Group->bAutomaticFront);
		Vector(Entry, TEXT("front"), Group->FrontLocation);
		Vector(Entry, TEXT("center"), Group->GetCenter());
		Vector(Entry, TEXT("destination"), Group->Destination);
		Vector(Entry, TEXT("home"), Group->HomeLocation);
		auto Units = TArray<TSharedPtr<FJsonValue>>();
		for (const AArmyUnit* Unit : Group->Units)
		{
			if (!IsValid(Unit)) continue;
			auto Member = Object();
			Number(Member, TEXT("actorId"), LifetimeId(Unit));
			Number(Member, TEXT("maxHealth"), Unit->MaxHealth());
			Number(Member, TEXT("slot"), Unit->CompositionSlot);
			Number(Member, TEXT("role"), static_cast<int32>(Unit->UnitRole));
			Number(Member, TEXT("health"), Unit->Health);
			Number(Member, TEXT("attacks"), Unit->AttackCount);
			Number(Member, TEXT("owner"), Unit->CommanderIndex);
			Member->SetBoolField(TEXT("reinforcing"), Unit->bReinforcing);
			Number(Member, TEXT("producer"), IsValid(Group->ProductionBuilding) ? State->Buildings.IndexOfByKey(Group->ProductionBuilding) : -1);
			Vector(Member, TEXT("position"), Unit->GetActorLocation());
			Units.Add(MakeShared<FJsonValueObject>(Member));
		}
		Entry->SetArrayField(TEXT("units"), Units);
		Armies.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("armies"), Armies);
	auto Buildings = TArray<TSharedPtr<FJsonValue>>();
	for (int32 Index = 0; Index < State->Buildings.Num(); ++Index)
	{
		const ACommandBuilding* Building = State->Buildings[Index];
		if (!IsValid(Building)) continue;
		auto Entry = Object();
		Number(Entry, TEXT("index"), Index);
		Number(Entry, TEXT("actorId"), LifetimeId(Building));
		Number(Entry, TEXT("owner"), IsValid(Building->OwningPlayerState) ? Building->OwningPlayerState->CommanderIndex : -1);
		Number(Entry, TEXT("team"), Building->TeamIndex);
		Number(Entry, TEXT("kind"), static_cast<int32>(Building->Kind));
		Number(Entry, TEXT("health"), Building->Health);
		int32 Joined, Travelling;
		Building->GetForceCounts(Joined, Travelling);
		Entry->SetBoolField(TEXT("configured"), Building->bForceConfigured);
		Number(Entry, TEXT("forceID"), IsValid(Building->ForceGroup) ? Building->ForceGroup->ArmyIndex : -1);
		const UArmyUnitDefinition* Unit = ProductionDefinition(*State, *Building);
		Number(Entry, TEXT("capacity"), Unit ? Unit->Capacity : 0);
		Number(Entry, TEXT("joined"), Joined);
		Number(Entry, TEXT("travelling"), Travelling);
		Number(Entry, TEXT("unitCost"), Unit ? Unit->UnitCost : 0);
		Number(Entry, TEXT("unitTime"), Unit ? Unit->UnitDuration : 0.f);
		Number(Entry, TEXT("constructionProgress"), Building->ConstructionProgress);
		Number(Entry, TEXT("recipe"), static_cast<int32>(Building->ProductionRole));
		Number(Entry, TEXT("productionSeconds"), Building->ProductionProgressSeconds);
		Number(Entry, TEXT("frontOrder"), static_cast<int32>(Building->FrontOrder));
		Entry->SetBoolField(TEXT("enabled"), Building->bProductionEnabled);
		Entry->SetStringField(TEXT("productionState"),
			StaticEnum<EProductionState>()->GetNameStringByValue(static_cast<int64>(Building->GetProductionState())));
		Vector(Entry, TEXT("position"), Building->GetActorLocation());
		Vector(Entry, TEXT("front"), Building->FrontLocation);
		Buildings.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("buildings"), Buildings);
	auto Sites = TArray<TSharedPtr<FJsonValue>>();
	for (const ACapturePoint* Site : State->CaptureSites)
	{
		if (!IsValid(Site)) continue;
		auto Entry = Object();
		Number(Entry, TEXT("actorId"), LifetimeId(Site));
		Number(Entry, TEXT("index"), Site->SiteIndex);
		Number(Entry, TEXT("kind"), static_cast<int32>(Site->SiteKind));
		Number(Entry, TEXT("owner"), Site->ControllingTeam);
		Number(Entry, TEXT("progress"), Site->CaptureProgress);
		Vector(Entry, TEXT("position"), Site->GetActorLocation());
		Entry->SetBoolField(TEXT("friendlyPresent"), Site->bFriendlyPresent);
		Entry->SetBoolField(TEXT("enemyPresent"), Site->bEnemyPresent);
		Entry->SetBoolField(TEXT("established"), Site->IsEstablishedForTeam(Site->ControllingTeam));
		Sites.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Result->SetArrayField(TEXT("sites"), Sites);
	Number(Result, TEXT("friendlyHQ"), IsValid(State->FriendlyHeadquarters) ? State->FriendlyHeadquarters->Health : -1);
	Number(Result, TEXT("enemyHQ"), IsValid(State->EnemyHeadquarters) ? State->EnemyHeadquarters->Health : -1);
	if (IsValid(State->FriendlyHeadquarters)) Vector(Result, TEXT("friendlyHQPosition"), State->FriendlyHeadquarters->GetActorLocation());
	if (IsValid(State->EnemyHeadquarters)) Vector(Result, TEXT("enemyHQPosition"), State->EnemyHeadquarters->GetActorLocation());
	return Result;
}
AArmyGroup* FindArmy(UWorld* World, int32 Owner, int32 Index)
{
	for (TActorIterator<AArmyGroup> It(World); It; ++It)
		if (It->ArmyIndex == Index && IsValid(It->OwningPlayerState)
			&& It->OwningPlayerState->CommanderIndex == Owner) return *It;
	return nullptr;
}
ACommandPlayerController* LocalController(UWorld* World)
{
	for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
		if (It->IsLocalController()) return *It;
	return nullptr;
}
FString Execute(UWorld* World, const TSharedPtr<FJsonObject>& Request)
{
	if (!World) return TEXT("game world unavailable");
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (Action == TEXT("observe")) return FString();
	ACommandPlayerController* PC = LocalController(World);
	const ACommandPlayerState* Own = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
	const int32 Owner = static_cast<int32>(Request->GetIntegerField(TEXT("owner")));
	const int32 Index = static_cast<int32>(Request->GetIntegerField(TEXT("army")));
	ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (Action == TEXT("build") || Action == TEXT("production") || Action == TEXT("front")
		|| Action == TEXT("cancel") || Action == TEXT("research"))
	{
		if (!Own || Own->CommanderIndex < 0 || !State) return TEXT("local owning controller unavailable");
		if (Action == TEXT("build"))
			PC->ServerPlaceBuilding(static_cast<int32>(Request->GetIntegerField(TEXT("kind"))),
				FVector(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")), 5.f));
		else
		{
			const int32 BuildingIndex = Request->GetIntegerField(TEXT("building"));
			if (!State->Buildings.IsValidIndex(BuildingIndex) || !IsValid(State->Buildings[BuildingIndex]))
				return TEXT("building not replicated locally");
			ACommandBuilding* Building = State->Buildings[BuildingIndex];
			if (Action == TEXT("production")) PC->ServerConfigureProduction(Building,
				static_cast<EUnitRole>(Request->GetIntegerField(TEXT("recipe"))), Request->GetBoolField(TEXT("enabled")));
			else if (Action == TEXT("front")) PC->ServerAssignFront(Building,
				static_cast<EFrontOrder>(Request->GetIntegerField(TEXT("frontOrder"))),
				FVector(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")), 5.f));
			else if (Action == TEXT("cancel")) PC->ServerCancelBuilding(Building);
			else PC->ServerResearch(Building, static_cast<EArmyDoctrine>(Request->GetIntegerField(TEXT("choice"))));
		}
		return FString();
	}
	// Shared controller/HUD path checks, not native OS input. Commands still use the owning-controller RPCs.
	if (Action == TEXT("select") || Action == TEXT("hud") || Action == TEXT("hudClick") || Action == TEXT("key")
		|| Action == TEXT("screenshot") || Action == TEXT("resolution"))
	{
		if (!PC) return TEXT("local controller unavailable");
		if (Action == TEXT("select"))
		{
			const FString Target = Request->GetStringField(TEXT("target"));
			if (Target == TEXT("none")) PC->SelectActor(nullptr);
			else if (Target == TEXT("building"))
			{
				const int32 BuildingIndex = Request->GetIntegerField(TEXT("building"));
				if (!State || !State->Buildings.IsValidIndex(BuildingIndex) || !IsValid(State->Buildings[BuildingIndex]))
					return TEXT("building not replicated locally");
				PC->SelectActor(State->Buildings[BuildingIndex]);
				if (PC->GetSelectedBuilding() != State->Buildings[BuildingIndex]) return TEXT("building selection rejected");
			}
			else if (Target == TEXT("squad"))
				return TEXT("Squad selection removed; select its production building and configure a front instead.");
			else return TEXT("unknown selection target");
			return FString();
		}
		if (Action == TEXT("hud"))
		{
			const ACommandHUD* HUD = Cast<ACommandHUD>(PC->GetHUD());
			const EHUDAction HUDAction = static_cast<EHUDAction>(Request->GetIntegerField(TEXT("hudAction")));
			FVector2D Position;
			if (!HUD || !HUD->FindActionScreenPosition(HUDAction, Position)) return TEXT("HUD action not visible and available");
			if (HUD->GetActionAtScreenPosition(Position) != HUDAction) return TEXT("HUD hit test disagrees with drawn geometry");
			if (!PC->HandleHUDClick(Position)) return TEXT("HUD click missed every panel");
			return FString();
		}
		if (Action == TEXT("hudClick"))
		{
			const FVector2D Position(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")));
			if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y)) return TEXT("invalid HUD screen point");
			return PC->HandleHUDClick(Position) ? FString() : TEXT("HUD click missed every panel");
		}
		if (Action == TEXT("key"))
		{
			// Delivered to PlayerInput (Enhanced Input mappings), not the OS/compositor.
			const FString KeyName = Request->GetStringField(TEXT("key"));
			if (KeyName != TEXT("Escape") && KeyName != TEXT("F4") && KeyName != TEXT("Tab")
				&& KeyName != TEXT("Enter") && KeyName != TEXT("SpaceBar")
				&& KeyName != TEXT("Q") && KeyName != TEXT("H") && KeyName != TEXT("R")) return TEXT("unsupported probe key");
			FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
			PC->InputKey(FInputKeyEventArgs(Viewport, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),
				FKey(*KeyName), Request->GetBoolField(TEXT("pressed")) ? IE_Pressed : IE_Released, FPlatformTime::Cycles64()));
			return FString();
		}
		if (Action == TEXT("screenshot"))
		{
			const FString Path = Request->GetStringField(TEXT("path"));
			if (Path.IsEmpty()) return TEXT("screenshot path required");
			FScreenshotRequest::RequestScreenshot(Path, false, false);
			return FString();
		}
		const int32 Width = static_cast<int32>(Request->GetIntegerField(TEXT("width")));
		const int32 Height = static_cast<int32>(Request->GetIntegerField(TEXT("height")));
		if (Width < 640 || Height < 480 || Width > 7680 || Height > 4320) return TEXT("resolution out of bounds");
		PC->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"), Width, Height));
		return FString();
	}
	if (Action == TEXT("restart") || Action == TEXT("order") || Action == TEXT("attack"))
	{
		if (!Own || Own->CommanderIndex < 0) return TEXT("local owning controller unavailable");
		if (Action == TEXT("restart")) PC->ServerRequestRestart();
		else
		{
			AArmyGroup* Army = FindArmy(World, Owner, Index);
			if (!Army) return TEXT("target army not replicated locally");
			if (Action == TEXT("attack"))
			{
				if (!State || !IsValid(State->EnemyHeadquarters)) return TEXT("enemy HQ not replicated");
				PC->ServerIssueAttack(Army, State->EnemyHeadquarters->GetActorLocation(), State->EnemyHeadquarters);
			}
			else
			{
				FVector Destination(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y")), 0.f);
				PC->ServerIssueOrder(Army, static_cast<EArmyOrder>(Request->GetIntegerField(TEXT("order"))), Destination);
			}
		}
		return FString();
	}
	// No fixture execution on clients, even if a malicious client sends fixture commands.
	if (!bAuthorityFixtures || !World->GetAuthGameMode() || World->GetNetMode() != NM_ListenServer)
		return TEXT("host-only authority fixture switch required");
	if (!State) return TEXT("server fixture state unavailable");
	AArmyGroup* Army = FindArmy(World, Owner, Index);
	if (Action == TEXT("isolate"))
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (It->TeamIndex == 5) It->IssueHold();
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->TeamIndex == 5 && Building->IsProducer())
				Building->SetProduction(Building->ProductionUnitIndex, false);
		return FString();
	}
	if (Action == TEXT("placement"))
	{
		bPlacementCandidateValid = false;
		const int32 BuildingIndex = static_cast<int32>(Request->GetIntegerField(TEXT("kind")));
		if (!IsValid(State->FriendlyHeadquarters)) return TEXT("friendly HQ unavailable");
		const FVector Center = State->FriendlyHeadquarters->GetActorLocation();
		for (int32 Ring = 0; Ring < 5; ++Ring)
			for (int32 Direction = 0; Direction < 16; ++Direction)
			{
				const float Angle = Direction * PI / 8.f;
				FVector Candidate = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 120.f);
				Candidate.Z = 5.f;
				FString Reason;
				if (State->ValidateBuildingPlacement(BuildingIndex, 0, Candidate, Reason))
				{
					bPlacementCandidateValid = true;
					PlacementCandidate = Candidate;
					return FString();
				}
			}
		return TEXT("no valid construction candidate");
	}
	if (Action == TEXT("income"))
	{
		State->bVerificationIncomePaused = Request->GetBoolField(TEXT("paused"));
		return FString();
	}
	if (Action == TEXT("fund"))
	{
		ACommandPlayerState* Wallet = nullptr;
		for (APlayerState* Player : State->PlayerArray)
			if (auto* Candidate = Cast<ACommandPlayerState>(Player))
				if (Candidate->CommanderIndex == Owner) Wallet = Candidate;
		if (!Wallet) return TEXT("fixture wallet unavailable");
		const int32 Amount = static_cast<int32>(Request->GetIntegerField(TEXT("amount")));
		if (Amount < 0 || Amount > 1000) return TEXT("fixture wallet amount out of bounds");
		Wallet->Resources = Amount;
		Wallet->ForceNetUpdate();
		return FString();
	}
	if (!Army) return TEXT("server fixture army unavailable");
	if (Action == TEXT("capture"))
	{
		const int32 SiteIndex = static_cast<int32>(Request->GetIntegerField(TEXT("site")));
		ACapturePoint* Site = nullptr;
		for (ACapturePoint* Candidate : State->CaptureSites)
			if (IsValid(Candidate) && Candidate->SiteIndex == SiteIndex) Site = Candidate;
		if (!Site || Army->Units.IsEmpty()) return TEXT("capture site or unit unavailable");
		Army->Units[0]->SetActorLocation(Site->GetActorLocation() + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		return FString();
	}
	if (Action == TEXT("occupant"))
	{
		const int32 SiteIndex = static_cast<int32>(Request->GetIntegerField(TEXT("site")));
		const int32 Slot = static_cast<int32>(Request->GetIntegerField(TEXT("slot")));
		const bool bEnemy = Request->GetBoolField(TEXT("enemy"));
		const bool bPresent = Request->GetBoolField(TEXT("present"));
		ACapturePoint* Site = nullptr;
		for (ACapturePoint* Candidate : State->CaptureSites)
			if (IsValid(Candidate) && Candidate->SiteIndex == SiteIndex
				&& Candidate->SiteKind == ECaptureSiteKind::Resource) Site = Candidate;
		AArmyGroup* SelectedGroup = Army;
		if (bEnemy)
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->TeamIndex == 5) { SelectedGroup = *It; break; }
		if (!Site || !IsValid(SelectedGroup) || SelectedGroup->TeamIndex != (bEnemy ? 5 : 0))
			return TEXT("objective resource site or army unavailable");
		AArmyUnit* Unit = nullptr;
		for (AArmyUnit* Candidate : SelectedGroup->Units)
			if (IsValid(Candidate) && Candidate->IsAlive() && Candidate->CompositionSlot == Slot) Unit = Candidate;
		if (!Unit) return TEXT("objective live unit slot unavailable");
		// Only real units are moved; AdvanceCapture samples them on the normal server tick.
		const FVector Location = bPresent
			? Site->GetActorLocation() + FVector(bEnemy ? 370.f : -30.f, 0.f, 95.f)
			: SelectedGroup->HomeLocation + FVector(0.f, 0.f, 95.f);
		Unit->SetActorLocation(Location, false, nullptr, ETeleportType::TeleportPhysics);
		Unit->ForceNetUpdate();
		return FString();
	}
	if (Action == TEXT("assaultSetup"))
	{
		if (!IsValid(State->EnemyHeadquarters) || Army->Units.IsEmpty() || Army->TeamIndex != 0)
			return TEXT("objective friendly army or enemy HQ unavailable");
		if (!Army->IssueHold()) return TEXT("objective friendly army could not hold");
		const FVector Anchor = State->EnemyHeadquarters->GetActorLocation() + FVector(900.f, 700.f, 0.f);
		for (AArmyUnit* Unit : Army->Units)
		{
			if (!IsValid(Unit) || !Unit->IsAlive()) continue;
			const FVector Offset(0.f, Unit->CompositionSlot % 2 ? 110.f : -110.f, 0.f);
			Unit->SetActorLocation(Anchor + Offset, false, nullptr, ETeleportType::TeleportPhysics);
			Unit->ForceNetUpdate();
		}
		return FString();
	}
	if (Action == TEXT("kill"))
	{
		const int32 Slot = static_cast<int32>(Request->GetIntegerField(TEXT("slot")));
		AArmyUnit* Victim = nullptr;
		for (AArmyUnit* Unit : Army->Units) if (IsValid(Unit) && Unit->CompositionSlot == Slot) Victim = Unit;
		AArmyUnit* Shooter = nullptr;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (It->TeamIndex == 5 && !It->Units.IsEmpty()) { Shooter = It->Units[0]; break; }
		if (!IsValid(Shooter))
		{
			if (!IsValid(State->EnemyHeadquarters)) return TEXT("enemy HQ unavailable for hostile staging");
			// Hostile fixtures stage in front of the enemy HQ, never at a literal map coordinate.
			const FTransform Transform(FRotator::ZeroRotator,
				State->EnemyHeadquarters->GetActorLocation() + FVector(-1400.f, 0.f, -10.f));
			AArmyGroup* Hostile = World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
				nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
			if (!Hostile) return TEXT("hostile casualty fixture allocation failed");
			Hostile->TeamIndex = 5;
			Hostile->OwningPlayerState = State->EnemyCommander;
			Hostile->bOpposingArmy = true;
			Hostile->HomeLocation = Transform.GetLocation();
			Hostile->ArmyIndex = -1;
			Hostile->FinishSpawning(Transform);
			if (!Hostile->SpawnUnits()) { Hostile->Destroy(); return TEXT("hostile casualty fixture spawn failed"); }
			Hostile->IssueHold();
			Hostile->SetActorTickEnabled(false);
			for (AArmyUnit* Unit : Hostile->Units) Unit->SetActorTickEnabled(false);
			Shooter = Hostile->Units[0];
		}
		if (!Victim || !IsValid(Shooter)) return TEXT("live casualty or hostile shooter unavailable");
		Victim->ReceiveAttack(Victim->Health, Shooter);
		return !Victim->IsAlive() ? FString() : TEXT("hostile damage did not kill casualty");
	}
	if (Action == TEXT("finish"))
	{
		const bool bWin = Request->GetBoolField(TEXT("win"));
		AHeadquarters* Target = bWin ? State->EnemyHeadquarters : State->FriendlyHeadquarters;
		AArmyUnit* Shooter = bWin && !Army->Units.IsEmpty() ? Army->Units[0] : nullptr;
		if (!bWin)
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->TeamIndex == 5 && !It->Units.IsEmpty()) { Shooter = It->Units[0]; break; }
		if (!IsValid(Target) || !IsValid(Shooter)) return TEXT("HQ or shooter unavailable");
		Target->Health = 1;
		Target->ForceNetUpdate();
		const FVector Previous = Shooter->GetActorLocation();
		Shooter->SetActorLocation(Target->GetActorLocation() + FVector(90.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Shooter->NextAttackTime = 0.f;
		Shooter->FireAt(Target);
		Shooter->SetActorLocation(Previous, false, nullptr, ETeleportType::TeleportPhysics);
		return Target->Health == 0 ? FString() : TEXT("weapon did not destroy HQ");
	}
	return TEXT("unknown command");
}
bool Tick(float)
{
	UWorld* World = nullptr;
	if (GEngine)
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (UWorld* Candidate = Context.World())
				if (Candidate->IsGameWorld())
				{ World = Candidate; break; }
	if (World && World != LastWorld.Get())
	{
		LastWorld = World;
		++Generation;
		bPlacementCandidateValid = false;
	}
	FString Input;
	if (!FFileHelper::LoadFileToString(Input, *(Directory / TEXT("request.json")))) return true;
	TSharedPtr<FJsonObject> Request;
	if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Input), Request) || !Request.IsValid()) return true;
	int32 Id = 0;
	if (!Request->TryGetNumberField(TEXT("id"), Id) || Id <= LastCommand) return true;
	LastCommand = Id;
	auto Reply = Object();
	Number(Reply, TEXT("id"), Id);
	Reply->SetStringField(TEXT("peer"), Peer);
	const FString Error = Execute(World, Request);
	Reply->SetStringField(TEXT("error"), Error);
	Reply->SetObjectField(TEXT("state"), Snapshot(World));
	FString Output;
	FJsonSerializer::Serialize(Reply.ToSharedRef(), TJsonWriterFactory<>::Create(&Output));
	FFileHelper::SaveStringToFile(Output, *(Directory / TEXT("reply.tmp")));
	FPlatformFileManager::Get().GetPlatformFile().MoveFile(*(Directory / TEXT("reply.json")), *(Directory / TEXT("reply.tmp")));
	UE_LOG(LogTemp, Display, TEXT("Network verification peer=%s id=%d error=%s"), *Peer, Id, *Error);
	return true;
}
} // namespace
void Start()
{
	if (!FParse::Value(FCommandLine::Get(), TEXT("CoopRTSNetVerifyDir="), Directory)
		|| !FParse::Value(FCommandLine::Get(), TEXT("CoopRTSNetVerifyPeer="), Peer)) return;
	bAuthorityFixtures = FParse::Param(FCommandLine::Get(), TEXT("CoopRTSNetVerifyAuthority"));
	if (Directory.IsEmpty() || Peer.IsEmpty()) return;
	UE_LOG(LogTemp, Display, TEXT("Network verification enabled peer=%s authorityFixtures=%d"), *Peer, bAuthorityFixtures);
	TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateStatic(&Tick), .1f);
}
void Stop()
{
	if (TickHandle.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
}
} // namespace CoopRTSNetworkVerification
#endif
