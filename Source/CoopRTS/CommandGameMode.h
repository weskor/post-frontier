#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CommandGameMode.generated.h"
class ACommandPlayerController;
class AEnemyCommander;
class UMatchContent;


UCLASS()
class COOPRTS_API ACommandGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACommandGameMode();
	virtual void InitGameState() override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PreLogin(const FString& Options, const FString& Address,
		const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	void RequestRestart(ACommandPlayerController* Requester);
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
	// Catalogue handed to ACommandGameState::Content at BeginPlay; /Game/Content/DA_MatchContent by default.
	UPROPERTY(EditDefaultsOnly, Category = "Content")
	TObjectPtr<UMatchContent> DefaultContent;
private:
	UPROPERTY()
	TObjectPtr<AEnemyCommander> EnemyCommander;
	// False when the level lacks an arena or either headquarters; nobody gets a commander slot.
	bool bLevelValid = false;
	bool bRestartRequested = false;
	TSet<TWeakObjectPtr<APlayerController>> StartedCommanders;
};
