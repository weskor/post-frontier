#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "CommandGameMode.generated.h"
class ACommandPlayerController;
class AEnemyCommander;


UCLASS()
class COOPRTS_API ACommandGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	ACommandGameMode();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PreLogin(const FString& Options, const FString& Address,
		const FUniqueNetIdRepl& UniqueId, FString& ErrorMessage) override;
	void RequestRestart(ACommandPlayerController* Requester);
	virtual void HandleStartingNewPlayer_Implementation(APlayerController* NewPlayer) override;
	virtual void Logout(AController* Exiting) override;
private:
	UPROPERTY()
	TObjectPtr<AEnemyCommander> EnemyCommander;
	bool bRestartRequested = false;
	TSet<TWeakObjectPtr<APlayerController>> StartedCommanders;
};
