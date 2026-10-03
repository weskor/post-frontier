#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameFramework/OnlineReplStructs.h"
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
	// Opaque reconnect identity is memory-only; PlayerId is an unrelated UUID.
	FUniqueNetIdRepl OnlineId;
	FString PlayerId;
	int32 CommanderIndex = INDEX_NONE;
	int32 Orders = 0;
	int32 Builds = 0;
	int32 Pings = 0;
	double JoinedSeconds = 0.;
	double LeftSeconds = 0.;
	double ConnectedSinceSeconds = 0.;
	double ParticipationSeconds = 0.;
	bool bHasLeft = false;
	bool bDisconnected = false;
};

// Local, non-replicated product telemetry. Each fresh GameState owns a fresh
// component; only authority records humans and attempts a terminal/abandoned write.
UCLASS()
class COOPRTS_API UMatchTelemetry : public UActorComponent
{
	GENERATED_BODY()
public:
	UMatchTelemetry();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
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
	void WriteMatch(bool bAbandoned, const TCHAR* AbandonmentCause = nullptr);
	TArray<FMatchTelemetryPlayer> Players;
	FString MatchId;
	FString OutputPath;
	double StartedSimulationSeconds = 0.;
	double EndedBattleSeconds = 0.;
	bool bFlushed = false;
};
