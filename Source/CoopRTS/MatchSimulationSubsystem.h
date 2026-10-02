#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "MatchSimulationSubsystem.generated.h"

class AArmyUnit;
class ACommandGameState;
class ACommandPlayerState;
class ADepositSite;
class AEnemyCommander;
class FJsonObject;

// Standalone AI-vs-AI only. No simulation subsystem exists in ordinary play.
UCLASS()
class COOPRTS_API UMatchSimulationSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	struct FObservedUnit
	{
		int32 TeamSlot = 0;
		int32 Health = 0;
		uint32 Attacks = 0;
	};
	bool Start(ACommandGameState& State);
	void Observe(ACommandGameState& State);
	void Snapshot(ACommandGameState& State, double ScheduledTime);
	TSharedRef<FJsonObject> Event(const TCHAR* Kind, int32 Team = -1);
	bool Flush();
	void Finish(const TCHAR* Status, const TCHAR* Outcome, int32 Winner, const FString& Error = FString());
	TSharedPtr<FJsonObject> Report;
	TWeakObjectPtr<ACommandPlayerState> HumanCommander;
	TWeakObjectPtr<AEnemyCommander> Autopilot;
	TMap<TWeakObjectPtr<AArmyUnit>, FObservedUnit> ObservedUnits;
	TMap<int32, int32> RegionOwners;
	TSet<TWeakObjectPtr<ADepositSite>> Depleted;
	int32 Produced[2] = {};
	int32 Casualties[2] = {};
	int64 ObservedHealthLoss[2] = {};
	int64 Attacks[2] = {};
	int32 PreviousHQHealth[2] = {};
	bool FirstPlaced[2][2] = {};
	bool FirstComplete[2][2] = {};
	bool FirstCapture[2] = {};
	bool bStarted = false;
	bool bFinished = false;
	double StartWorldTime = 0.;
	double StartWallTime = 0.;
	double NextSnapshot = 30.;
	float MaxGameDelta = 0.f;
};
