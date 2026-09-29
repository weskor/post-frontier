#include "CommandGameMode.h"

#include "ArmyGroup.h"
#include "CapturePoint.h"
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
	bUseSeamlessTravel = false; // Restart must recreate the controller and its local selection state.
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
	if (!HasAuthority() || !IsValid(Requester) || Requester->GetWorld() != GetWorld()
		|| !Commander || Commander->CommanderIndex < 0
		|| !State || State->MatchResult == EMatchResult::Ongoing) return;
	// A world travel discards every group, site, wallet, planner commitment and
	// controller selection; the new Boot world constructs an independent match.
	GetWorld()->ServerTravel(TEXT("/Game/Maps/Boot?listen?Restart"), false);
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
		{FVector(600.f, -2200.f, 5.f), ECaptureSiteKind::Reinforcement}
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
	if (State)
	{
		for (const APlayerState* Player : State->PlayerArray)
		{
			const ACommandPlayerState* Commander = Cast<ACommandPlayerState>(Player);
			if (Commander && Commander->CommanderIndex >= 0 && Commander->CommanderIndex < UE_ARRAY_COUNT(Occupied))
				Occupied[Commander->CommanderIndex] = true;
		}
	}
	if (Occupied[0] && Occupied[1] && Occupied[2] && Occupied[3] && Occupied[4])
	{
		ErrorMessage = TEXT("Match full (five commanders maximum)");
		return;
	}
	Super::PreLogin(Options, Address, UniqueId, ErrorMessage);
}

void ACommandGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	ACommandGameState* State = GetGameState<ACommandGameState>();
	ACommandPlayerState* Commander = NewPlayer ? NewPlayer->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!State || !Commander || State->MatchResult != EMatchResult::Ongoing) return;

	bool Occupied[5] = {};
	for (const APlayerState* Player : State->PlayerArray)
	{
		const ACommandPlayerState* Other = Cast<ACommandPlayerState>(Player);
		if (Other && Other != Commander && Other->CommanderIndex >= 0
			&& Other->CommanderIndex < UE_ARRAY_COUNT(Occupied))
			Occupied[Other->CommanderIndex] = true;
	}
	int32 Slot = 0;
	while (Slot < UE_ARRAY_COUNT(Occupied) && Occupied[Slot]) ++Slot;
	if (Slot == UE_ARRAY_COUNT(Occupied))
	{
		if (GameSession) GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("Match full (five commanders maximum)")));
		return;
	}
	Commander->CommanderIndex = Slot;
	Commander->SetPlayerName(FString::Printf(TEXT("Commander %d"), Slot + 1));
	Commander->ForceNetUpdate();
	// Super::PostLogin normally starts the default camera; retry if a map spawn failed.
	if (!NewPlayer->GetPawn()) RestartPlayer(NewPlayer);

	static const float HomeY[] = {0.f, -850.f, 850.f, -1700.f, 1700.f};
	const FVector Home(-1800.f, HomeY[Slot], 100.f);
	bool bSpawned = true;
	for (int32 ArmyIndex = 0; ArmyIndex < 2; ++ArmyIndex)
	{
		const FVector ArmyHome = Home + FVector(-1000.f * ArmyIndex, 0.f, 0.f);
		const FTransform Transform(FRotator::ZeroRotator, ArmyHome);
		AArmyGroup* Group = GetWorld()->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
			NewPlayer, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Group)
		{
			bSpawned = false;
			break;
		}
		Group->HomeLocation = ArmyHome;
		Group->TeamIndex = 0;
		Group->ArmyIndex = ArmyIndex;
		Group->OwningPlayerState = Commander;
		Group->FinishSpawning(Transform);
		if (!Group->SpawnUnits())
		{
			Group->Destroy();
			bSpawned = false;
			break;
		}
	}
	if (!bSpawned || !NewPlayer->GetPawn())
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to initialize commander %d for %s"), Slot, *NewPlayer->GetName());
		for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
			if (It->OwningPlayerState == Commander) It->Destroy();
		Commander->CommanderIndex = -1;
		Commander->ForceNetUpdate();
		if (GameSession) GameSession->KickPlayer(NewPlayer, FText::FromString(TEXT("Unable to spawn commander")));
		return;
	}
	UE_LOG(LogTemp, Display, TEXT("Commander joined slot=%d player=%s home=%s"),
		Slot, *Commander->GetPlayerName(), *Home.ToCompactString());


	// Spawn one strategic army and its commander for the whole match.
	bool bEnemyExists = false;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
		if (It->bOpposingArmy) { bEnemyExists = true; break; }
	if (!bEnemyExists)
	{
		const FVector EnemyHome(1800.f, 2300.f, 100.f);
		const FTransform EnemyTransform(FRotator::ZeroRotator, EnemyHome);
		AArmyGroup* Enemy = GetWorld()->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(),
			EnemyTransform, nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (Enemy)
		{
			Enemy->HomeLocation = EnemyHome;
			Enemy->TeamIndex = 5;
			Enemy->ArmyIndex = -1;
			Enemy->bOpposingArmy = true;
			Enemy->FinishSpawning(EnemyTransform);
			if (!Enemy->SpawnUnits())
			{
				UE_LOG(LogTemp, Error, TEXT("Failed to spawn complete opposing army"));
				Enemy->Destroy();
			}
			else
			{
				EnemyCommander = GetWorld()->SpawnActor<AEnemyCommander>();
				if (EnemyCommander) EnemyCommander->Army = Enemy;
				UE_LOG(LogTemp, Display, TEXT("Enemy strategic army team=5 center=%s roles=2 frontline/2 ranged/2 siege"),
					*Enemy->GetCenter().ToCompactString());
			}
		}
	}
}

void ACommandGameMode::Logout(AController* Exiting)
{
	ACommandPlayerState* Commander = Exiting ? Exiting->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (Commander)
	{
		for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
			if (It->OwningPlayerState == Commander) It->Destroy();
		UE_LOG(LogTemp, Display, TEXT("Commander left slot=%d"), Commander->CommanderIndex);
		Commander->CommanderIndex = -1;
	}
	Super::Logout(Exiting);
}
