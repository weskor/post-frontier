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
class AArmyGroup;
class AHeadquarters;
class AActor;

// Shared authoritative encounter runner for standalone measurement and worlds.
class COOPRTS_API FSimulationDuelRunner
{
public:
	FSimulationDuelRunner();
	~FSimulationDuelRunner();
	FSimulationDuelRunner(const FSimulationDuelRunner&) = delete;
	FSimulationDuelRunner& operator=(const FSimulationDuelRunner&) = delete;
	bool Start(ACommandGameState& State, int32 Seed);
	void Tick(float DeltaTime, float TimeCap);
	bool IsComplete() const { return bComplete; }
	const FString& GetError() const { return Error; }
	TSharedRef<FJsonObject> GetReport() const;

private:
	struct FMember
	{
		TWeakObjectPtr<AArmyUnit> Unit;
		int32 Side = 0;
		int32 Health = 0;
		uint32 Attacks = 0;
	};
	struct FPausedActor
	{
		TWeakObjectPtr<AActor> Actor;
		bool bTickEnabled = false;
	};
	bool FindGround();
	bool StartPair();
	void Observe();
	void UpdateRow() const;
	void ClearPair();
	void PauseActor(AActor& Actor);
	void RestoreHeadquarters();
	TWeakObjectPtr<ACommandGameState> State;
	TWeakObjectPtr<ACommandPlayerState> Wallets[2];
	TWeakObjectPtr<AHeadquarters> Headquarters[2];
	bool HQCollision[2] = {};
	TArray<int32> Definitions;
	TArray<TWeakObjectPtr<AArmyGroup>> Groups;
	TArray<FMember> Members;
	TArray<FPausedActor> PausedActors;
	TSharedRef<FJsonObject> Report;
	TSharedPtr<FJsonObject> Current;
	FRandomStream Random;
	FVector Center = FVector::ZeroVector;
	int32 PairIndex = 0;
	int32 SpawnFirstSide = 0;
	int32 Initial[2] = {};
	int32 Spent[2] = {};
	int32 Survivors[2] = {};
	int64 Damage[2] = {};
	int64 Attacks[2] = {};
	double Elapsed = 0.;
	double LastDamageElapsed = 0.;
	double StallTimeout = 30.;
	double TotalElapsed = 0.;
	bool bStarted = false;
	bool bComplete = false;
	FString Error;
};

// Explicit standalone match/duel opt-in only; absent from ordinary play.
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
	TUniquePtr<FSimulationDuelRunner> DuelRunner;
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
