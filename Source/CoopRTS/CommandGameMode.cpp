#include "CommandGameMode.h"

#include "ArmyGroup.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "ArmyUnit.h"
#include "CommandGameState.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "CommandPlayerState.h"
#include "CommandCamera.h"
#include "CommandHUD.h"
#include "CommandPlayerController.h"
#include "GameFramework/GameSession.h"
#include "Engine/World.h"
#include "EngineUtils.h"

ACommandGameMode::ACommandGameMode()
{
	PrimaryActorTick.bCanEverTick = true;
	bUseSeamlessTravel = true;
	PlayerControllerClass = ACommandPlayerController::StaticClass();
	DefaultPawnClass = ACommandCamera::StaticClass();
	HUDClass = ACommandHUD::StaticClass();
	GameStateClass = ACommandGameState::StaticClass();
	PlayerStateClass = ACommandPlayerState::StaticClass();
}

void ACommandGameMode::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ACommandGameState* State = GetGameState<ACommandGameState>();
	if (!State || State->MatchResult != EMatchResult::Ongoing
		|| !IsValid(State->FriendlyHeadquarters) || !IsValid(State->EnemyHeadquarters)) return;
	// Both lethal events in the same world frame resolve as defeat.
	const bool bFriendlyLost = !State->FriendlyHeadquarters->IsAlive();
	const bool bEnemyLost = !State->EnemyHeadquarters->IsAlive();
	if (!bFriendlyLost && !bEnemyLost) return;
	State->MatchResult = bFriendlyLost ? EMatchResult::Defeat : EMatchResult::Victory;
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
		|| !Commander || Commander->CommanderIndex < 0
		|| !State || State->MatchResult == EMatchResult::Ongoing) return;
	// Seamless travel keeps the net driver and player connections. Boot's new GameState
	// and actors are fresh; carried PlayerStates are reset when their controllers start.
	// Explicit SeamlessTravel avoids the engine's 48-hour automatic hard-travel fallback.
	bRestartRequested = true;
	if (!GetWorld()->ServerTravel(TEXT("/Game/Maps/Boot?listen?SeamlessTravel"), false))
		bRestartRequested = false;
}

void ACommandGameMode::BeginPlay()
{
	Super::BeginPlay();
	ACommandGameState* State = GetGameState<ACommandGameState>();
	if (!State) return;
	struct FSitePlacement { FVector Location; ECaptureSiteKind Kind; };
	const FSitePlacement Sites[] = {
		{FVector(-850.f, -1800.f, 5.f), ECaptureSiteKind::Resource},
		{FVector(1450.f, 1100.f, 5.f), ECaptureSiteKind::Resource},
		{FVector(600.f, -2200.f, 5.f), ECaptureSiteKind::Resource}
	};
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Sites); ++Index)
	{
		const FTransform Transform(Sites[Index].Location);
		ACapturePoint* Site = GetWorld()->SpawnActorDeferred<ACapturePoint>(ACapturePoint::StaticClass(),
			Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Site) continue;
		Site->SiteKind = Sites[Index].Kind;
		Site->SiteIndex = Index;
		Site->FinishSpawning(Transform);
		State->CaptureSites.Add(Site);
	}
	auto SpawnHQ = [this](FVector Location, int32 Team)
	{
		const FTransform Transform(Location);
		AHeadquarters* HQ = GetWorld()->SpawnActorDeferred<AHeadquarters>(AHeadquarters::StaticClass(),
			Transform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (HQ)
		{
			HQ->TeamIndex = Team;
			HQ->FinishSpawning(Transform);
		}
		return HQ;
	};
	State->FriendlyHeadquarters = SpawnHQ(FVector(-3500.f, -600.f, 110.f), 0);
	State->EnemyHeadquarters = SpawnHQ(FVector(3200.f, 2300.f, 110.f), 5);
	EnemyCommander = GetWorld()->SpawnActor<AEnemyCommander>();
	State->ForceNetUpdate();
}

void ACommandGameMode::PreLogin(const FString& Options, const FString& Address,
	const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage)
{
	const ACommandGameState* State = GetGameState<ACommandGameState>();
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

void ACommandGameMode::HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer)
{
	ACommandGameState* State = GetGameState<ACommandGameState>();
	ACommandPlayerState* Commander = NewPlayer ? NewPlayer->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!State || !Commander || State->MatchResult != EMatchResult::Ongoing) return;
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
			if (It->OwningPlayerState == Commander) It->Destroy();
		for (TActorIterator<ACommandBuilding> It(GetWorld()); It; ++It)
			if (It->OwningPlayerState == Commander) It->Destroy();
		UE_LOG(LogTemp, Display, TEXT("Commander left slot=%d"), Commander->CommanderIndex);
		Commander->CommanderIndex = -1;
	}
	Super::Logout(Exiting);
}
