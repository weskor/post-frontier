#include "CommandGameMode.h"

#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "DepositSite.h"
#include "CommandPlayerState.h"
#include "CommandCamera.h"
#include "CommandHUD.h"
#include "CommandPlayerController.h"
#include "GameFramework/GameSession.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Content/MatchContent.h"
#include "UObject/ConstructorHelpers.h"
#include "Rules/OutcomePolicy.h"

ACommandGameMode::ACommandGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseSeamlessTravel = true;
	PlayerControllerClass = ACommandPlayerController::StaticClass();
	DefaultPawnClass = ACommandCamera::StaticClass();
	HUDClass = ACommandHUD::StaticClass();
	GameStateClass = ACommandGameState::StaticClass();
	PlayerStateClass = ACommandPlayerState::StaticClass();
	static ConstructorHelpers::FObjectFinder<UMatchContent> Content(TEXT("/Game/Content/DA_MatchContent.DA_MatchContent"));
	if (Content.Succeeded()) DefaultContent = Content.Object;
}

void ACommandGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACommandGameState* State = GetGameState<ACommandGameState>();
	if (!State || State->MatchResult != EMatchResult::Ongoing
		|| !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters)) return;
	const EMatchResult Result = OutcomePolicy::Evaluate({ State->FriendlyHeadquarters->Health,
		State->EnemyHeadquarters->Health, EMatchResult::Ongoing, EMatchResult::Victory, EMatchResult::Defeat });
	if (Result == EMatchResult::Ongoing) return;
	const bool bFriendlyLost = Result == EMatchResult::Defeat;
	State->SetMatchResult(Result);
	State->EnemyPlan = TEXT("MATCH COMPLETE");
	State->EnemyPlanRationale = bFriendlyLost ? TEXT("Friendly HQ destroyed (ties are defeat)")
		: TEXT("Enemy HQ destroyed");
	State->ForceNetUpdate();
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It) It->SettleMatch();
	UE_LOG(LogTemp, Display, TEXT("Match result=%s friendlyHQ=%d enemyHQ=%d"),
		bFriendlyLost ? TEXT("Defeat") : TEXT("Victory"),
		State->FriendlyHeadquarters->Health, State->EnemyHeadquarters->Health);
}

void ACommandGameMode::RequestRestart(ACommandPlayerController* Requester)
{
	const ACommandGameState* State = GetGameState<ACommandGameState>();
	const ACommandPlayerState* Commander = IsValid(Requester)
		? Requester->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!HasAuthority() || bRestartRequested || !IsValid(Requester) || Requester->GetWorld() != GetWorld()
		|| !Commander || Commander->TeamIndex != 0 || Commander->CommanderIndex < 0 || Commander->CommanderIndex >= 5
		|| !State || State->MatchResult == EMatchResult::Ongoing) return;
	// Seamless travel keeps the net driver and player connections. The new level's GameState
	// and actors are fresh; carried PlayerStates are reset when their controllers start.
	// Explicit SeamlessTravel avoids the engine's 48-hour automatic hard-travel fallback.
	bRestartRequested = true;
	const FString Map = UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName());
	const FString Options = GetNetMode() == NM_Standalone ? TEXT("?SeamlessTravel") : TEXT("?listen?SeamlessTravel");
	if (!GetWorld()->ServerTravel(Map + Options, false))
		bRestartRequested = false;
}

void ACommandGameMode::InitGameState()
{
	Super::InitGameState();
	// Discover placed match actors before the host's own login; ordering in the level is irrelevant.
	ACommandGameState* State = GetGameState<ACommandGameState>();
	if (!State) return;
	for (TActorIterator<AArenaBounds> It(GetWorld()); It; ++It)
	{
		if (State->Arena) { UE_LOG(LogTemp, Error, TEXT("Level places more than one ArenaBounds; using %s"), *State->Arena->GetName()); }
		else State->Arena = *It;
	}
	for (TActorIterator<AHeadquarters> It(GetWorld()); It; ++It)
	{
		TObjectPtr<AHeadquarters>& Slot = It->TeamIndex == 5 ? State->EnemyHeadquarters : State->FriendlyHeadquarters;
		if (Slot) { UE_LOG(LogTemp, Error, TEXT("Level places more than one team %d Headquarters; using %s"), It->TeamIndex, *Slot->GetName()); }
		else Slot = *It;
	}
	for (TActorIterator<ACapturePoint> It(GetWorld()); It; ++It) State->CaptureSites.Add(*It);
	// Fixtures address sectors by index (CaptureSites[0]); placement order in the level is irrelevant.
	State->CaptureSites.Sort([](const ACapturePoint& A, const ACapturePoint& B) { return A.SiteIndex < B.SiteIndex; });
	for (TActorIterator<AMapRegion> It(GetWorld()); It; ++It) State->Regions.Add(*It);
	State->Regions.Sort([](const AMapRegion& A, const AMapRegion& B) { return A.RegionIndex < B.RegionIndex; });
	for (AMapRegion* Region : State->Regions)
	{
		if (Region->RegionRole == ERegionRole::Main) Region->Anchor = nullptr;
		else if (!IsValid(Region->Anchor))
			for (ACapturePoint* Site : State->CaptureSites)
				if (Site->SiteIndex == Region->RegionIndex) { Region->Anchor = Site; break; }
	}
	for (TActorIterator<ADepositSite> It(GetWorld()); It; ++It) State->Deposits.Add(*It);
	State->Deposits.Sort([](const ADepositSite& A, const ADepositSite& B)
	{
		if (A.RegionIndex != B.RegionIndex) return A.RegionIndex < B.RegionIndex;
		const FVector ALocation = A.GetActorLocation(), BLocation = B.GetActorLocation();
		return ALocation.X != BLocation.X ? ALocation.X < BLocation.X : ALocation.Y < BLocation.Y;
	});
	bLevelValid = State->Arena && State->FriendlyHeadquarters && State->EnemyHeadquarters
		&& !State->Regions.IsEmpty() && !State->Deposits.IsEmpty();
	if (!bLevelValid)
		UE_LOG(LogTemp, Error, TEXT("Map %s cannot start a match: arena=%d friendlyHQ=%d enemyHQ=%d sectors=%d regions=%d deposits=%d"),
			*GetWorld()->GetMapName(), State->Arena != nullptr, State->FriendlyHeadquarters != nullptr,
			State->EnemyHeadquarters != nullptr, State->CaptureSites.Num(), State->Regions.Num(), State->Deposits.Num());
	// Set the team before PlayerState registration; only human states survive seamless travel.
	const FTransform Transform = FTransform::Identity;
	State->EnemyCommander = GetWorld()->SpawnActorDeferred<ACommandPlayerState>(ACommandPlayerState::StaticClass(),
		Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (ACommandPlayerState* Commander = State->EnemyCommander)
	{
		Commander->CommanderIndex = -1;
		Commander->TeamIndex = 5;
		Commander->FinishSpawning(Transform);
		Commander->ResetForNewMatch();
	}
}

void ACommandGameMode::BeginPlay()
{
	Super::BeginPlay();
	ACommandGameState* State = GetGameState<ACommandGameState>();
	if (!State) return;
	State->Content = DefaultContent;
	if (!bLevelValid) return;
	EnemyCommander = GetWorld()->SpawnActor<AEnemyCommander>();
	State->ForceNetUpdate();
}

void ACommandGameMode::PreLogin(const FString& Options, const FString& Address,
	const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	const ACommandGameState* State = GetGameState<ACommandGameState>();
	if (!bLevelValid)
	{
		ErrorMessage = TEXT("Map lacks an arena, headquarters, regions or deposits");
		return;
	}
	if (State && State->MatchResult != EMatchResult::Ongoing)
	{
		ErrorMessage = TEXT("Match finished; rejoin after restart");
		return;
	}
	bool Occupied[5] = {};
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const ACommandPlayerState* Commander = It->Get()
			? It->Get()->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (Commander && Commander->CommanderIndex >= 0 && Commander->CommanderIndex < UE_ARRAY_COUNT(Occupied))
			Occupied[Commander->CommanderIndex] = true;
	}
	if (Occupied[0] && Occupied[1] && Occupied[2] && Occupied[3] && Occupied[4])
	{
		ErrorMessage = TEXT("Match full (five commanders maximum)");
		return;
	}
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
}

void ACommandGameMode::HandleSeamlessTravelPlayer(AController*& Controller)
{
	Super::HandleSeamlessTravelPlayer(Controller);
	if (ACommandPlayerState* Commander = Controller ? Controller->GetPlayerState<ACommandPlayerState>() : nullptr)
		Commander->ResetForNewMatch();
}

void ACommandGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	ACommandGameState* State = GetGameState<ACommandGameState>();
	ACommandPlayerState* Commander = NewPlayer ? NewPlayer->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!State || !Commander || !bLevelValid || State->MatchResult != EMatchResult::Ongoing) return;
	// A commander can legitimately own no squads or buildings. Initialization is
	// a controller lifecycle fact, not inferred from production actors.
	if (StartedCommanders.Contains(NewPlayer)) return;

	Super::HandleStartingNewPlayer_Implementation(NewPlayer);
	bool Occupied[5] = {};
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		const ACommandPlayerState* Other = It->Get() && It->Get() != NewPlayer
			? It->Get()->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (Other && Other->CommanderIndex >= 0 && Other->CommanderIndex < UE_ARRAY_COUNT(Occupied))
			Occupied[Other->CommanderIndex] = true;
	}
	const int32 PreviousSlot = Commander->CommanderIndex;
	int32 Slot = PreviousSlot >= 0 && PreviousSlot < UE_ARRAY_COUNT(Occupied) && !Occupied[PreviousSlot]
		? PreviousSlot : 0;
	while (Slot < UE_ARRAY_COUNT(Occupied) && Occupied[Slot]) ++Slot;
	if (Slot == UE_ARRAY_COUNT(Occupied))
	{
		if (GameSession) GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("Match full (five commanders maximum)")));
		return;
	}
	Commander->ResetForNewMatch();
	Commander->CommanderIndex = Slot;
	Commander->TeamIndex = 0;
	Commander->SetPlayerName(FString::Printf(TEXT("Commander %d"), Slot + 1));
	Commander->ForceNetUpdate();
	// The engine starts a new camera for initial and seamless players alike.
	if (!NewPlayer->GetPawn()) RestartPlayer(NewPlayer);

	if (!NewPlayer->GetPawn())
	{
		Commander->CommanderIndex = -1;
		Commander->ForceNetUpdate();
		if (GameSession) GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("Unable to spawn commander")));
		return;
	}
	StartedCommanders.Add(NewPlayer);
	UE_LOG(LogTemp, Display, TEXT("Commander joined slot=%d player=%s; construction ready"),
		Slot, *Commander->GetPlayerName());

}

void ACommandGameMode::Logout(AController* Exiting)
{
	ACommandPlayerState* Commander = Exiting ? Exiting->GetPlayerState<ACommandPlayerState>() : nullptr;
	StartedCommanders.Remove(Cast<APlayerController>(Exiting));
	if (Commander)
	{
		for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
			if (It->GetOwningPlayerState() == Commander) It->Destroy();
		for (TActorIterator<ACommandBuilding> It(GetWorld()); It; ++It)
			if (It->OwningPlayerState == Commander) It->Destroy();
		UE_LOG(LogTemp, Display, TEXT("Commander left slot=%d"), Commander->CommanderIndex);
		Commander->CommanderIndex = -1;
	}
	Super::Logout(Exiting);
}
