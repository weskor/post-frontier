#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "CoreMinimal.h"

class UWorld;
class ACommandGameState;
class ACommandPlayerController;
class ACommandPlayerState;
class AArmyGroup;
class UMatchTelemetry;
class FJsonObject;
class FJsonValue;

namespace MatchTelemetryScenarioTests
{
// Fresh standalone matches; only explicit fixtures act. Exercise real accepted
// commands, logout/reconnect identity, HQ outcomes, restart and ongoing travel.
class FMatchScenario : public IAutomationLatentCommand
{
public:
	explicit FMatchScenario(FAutomationTestBase* InTest);
	~FMatchScenario() override;

	bool Update() override;

private:
	bool Check(bool Value, const TCHAR* Message);
	bool Fail(const TCHAR* Message);
	FString PathFor(const UMatchTelemetry* Telemetry) const;
	void Track(ACommandGameState* State);
	void Cleanup();
	void Leave(UWorld* World, ACommandPlayerController* PC);
	bool EndWithHQ(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State);
	bool OriginalUnchanged();
	void Isolate(UWorld* World, ACommandGameState* State);
	void Freeze(AArmyGroup* Force);
	ACommandPlayerController* Participant(UWorld* World, ACommandGameState* State, int32 Index, const TCHAR* Name,
		const TCHAR* OnlineId = nullptr);
	bool Setup(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State);
	bool FindPlacement(ACommandGameState* State, int32 Team, const FVector& Center, FVector& Location);
	bool Commands(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State);
	bool IsNull(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key) const;
	bool ReadEvidence(const FString& Path, TSharedPtr<FJsonObject>& Root);
	bool ValidateAbandoned();
	bool ValidateFile(ACommandGameState* State, bool bFresh);
	bool Stage0(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Host);
	bool Stage1(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State);
	bool Stage2(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State);
	bool Stage3(UWorld* World, ACommandGameState* State);
	bool Stage4(UWorld* World, ACommandGameState* State);
	bool Stage5(UWorld* World, ACommandGameState* State);
	bool Stage6(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Host);
	bool Stage7(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State);
	bool Stage8(ACommandPlayerController* PC, ACommandGameState* State);
	bool Stage9(UWorld* World, ACommandGameState* State);
	bool Stage10(UWorld* World, ACommandGameState* State);
	bool Stage11(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State);
	bool ValidatePlayers(const TSharedPtr<FJsonObject>& Root, double Seconds, bool bFresh);
	bool ValidatePlayer(const TSharedPtr<FJsonValue>& Value, double Seconds, bool bFresh, TSet<FString>& Ids, bool (&Seen)[4]);
	bool ValidatePlayerRates(const TSharedPtr<FJsonObject>& Player, bool bFresh, bool bHost, int32 Index, double ExpectedParticipation);

	FAutomationTestBase* Test;
	double Started;
	double StageStarted = 0.;
	double PausedSeconds = 0.;
	double OriginalSeconds = 0.;
	double ZeroJoined = 0., FirstJoined = 0., FirstLeft = 0., Rejoined = 0., LastLeft = 0., InstantJoined = 0.;
	bool bCleaned = false;
	const TCHAR* PrivateOnlineId = TEXT("telemetry-private-hostname-76561198012345678");
	TSet<FString> CreatedPaths;
	TArray<TWeakObjectPtr<UMatchTelemetry>> TrackedTelemetry;
	TSet<FString> OriginalPlayerIds;
	FString AbandonedId, AbandonedPath, AbandonedMap, AbandonedJson;
	FDateTime AbandonedTimestamp;
	int32 Stage = 0;
	int32 HostIndex = INDEX_NONE;
	FString HostName, OriginalId, OriginalPath, OriginalJson;
	FDateTime OriginalTimestamp;
	FVector BuildLocation = FVector::ZeroVector;
	TWeakObjectPtr<ACommandGameState> OriginalState, RestartState, AbandonedState;
	TWeakObjectPtr<UMatchTelemetry> OriginalTelemetry, AbandonedTelemetry;
	TWeakObjectPtr<ACommandPlayerController> ZeroPlayer, Leaver;
	TWeakObjectPtr<AArmyGroup> FirstForce, SecondForce, EnemyForce, LeavingForce;
};
}

#endif
