#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MatchTelemetry.generated.h"

class ACommandGameState;
class ACommandPlayerState;

// One accepted player decision, independent of the number of selected forces.
enum class EMatchDecision : uint8
{
	Order,
	Build,
	Ping
};

struct FMatchTelemetryPlayer
{
	TWeakObjectPtr<ACommandPlayerState> State;
	FString PlayerId;
	FString PlayerName;
	int32 CommanderIndex = INDEX_NONE;
	int32 Orders = 0;
	int32 Builds = 0;
	int32 Pings = 0;
	bool bDisconnected = false;
};

// Local, non-replicated product telemetry. Each fresh GameState owns a fresh
// component; only authority records humans and attempts a terminal JSON write.
UCLASS()
class COOPRTS_API UMatchTelemetry : public UActorComponent
{
	GENERATED_BODY()
public:
	UMatchTelemetry();
	virtual void BeginPlay() override;
	void RegisterHuman(ACommandPlayerState* Player);
	void HumanLeft(ACommandPlayerState* Player);
	void RecordAccepted(ACommandPlayerState* Player, EMatchDecision Decision);
	void FlushMatch();
	const FString& GetMatchId() const { return MatchId; }
	const FString& GetOutputPath() const { return OutputPath; }
	double GetBattleSeconds() const;

private:
	ACommandGameState* AuthorityState() const;
	FMatchTelemetryPlayer* FindOrRegister(ACommandPlayerState* Player);
	TArray<FMatchTelemetryPlayer> Players;
	FString MatchId;
	FString OutputPath;
	double StartedSimulationSeconds = 0.;
	double EndedBattleSeconds = 0.;
	bool bFlushed = false;
};
