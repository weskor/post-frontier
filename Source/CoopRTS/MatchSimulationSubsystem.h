#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"
#include "Tickable.h"

class AArmyUnit;
class ACommandGameState;
class ACommandPlayerState;
class ADepositSite;
class AEnemyCommander;
class FJsonObject;
class FJsonValue;
class AArmyGroup;
class AHeadquarters;
class AActor;

// Shared authoritative encounter runner for standalone measurement and worlds.
// Setup lives in MatchSimulationDuelRunner.cpp, per-tick stepping in MatchSimulationDuelRunnerStep.cpp.
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
	bool FindWallets(ACommandGameState& InState);
	bool CollectDefinitions(ACommandGameState& InState, TArray<TSharedPtr<FJsonValue>>& Rows);
	void IsolateWorld(ACommandGameState& InState);
	bool FindGround();
	void RecordGeometry();
	bool StartPair();
	bool SpawnSide(int32 Side, int32 DefinitionIndex, const FVector& Forward, const FVector& Across,
		TArray<TSharedPtr<FJsonValue>>& Spawns);
	bool IssueAttackOrders();
	void PublishPair(TArray<TSharedPtr<FJsonValue>> Spawns[2]);
	void Observe();
	void UpdateRow() const;
	void FailStalled();
	void CompletePair(bool bWiped);
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
	double LastDamageElapsed = -1.; // Unarmed until the first attack or HP loss.
	double StallTimeout = 30.;
	double TotalElapsed = 0.;
	bool bStarted = false;
	bool bComplete = false;
	FString Error;
};

class UWorld;

// Non-reflected so the entire runner, not only its hooks, is absent from Shipping.
// Lifecycle: MatchSimulationSubsystem.cpp; match mode: MatchSimulationMatch.cpp;
// duel mode: MatchSimulationDuel.cpp; observation: MatchSimulationObserve.cpp;
// report rows and persistence: MatchSimulationTelemetry.cpp.
class FMatchSimulation final : public FTickableGameObject
{
public:
	explicit FMatchSimulation(UWorld* InWorld);
	virtual ~FMatchSimulation() override;
	virtual bool IsTickable() const override { return !bFinished; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return World; }
	virtual void Tick(float DeltaTime) override;
	virtual TStatId GetStatId() const override;

private:
	UWorld* GetWorld() const { return World; }
	UWorld* World;
	struct FObservedUnit
	{
		int32 TeamSlot = 0;
		int32 Health = 0;
		uint32 Attacks = 0;
	};
	// How a tick's state ends the match, if it does. The one place that interprets outcomes.
	struct FOutcome
	{
		bool bEnded = false;
		const TCHAR* Kind = TEXT("none");
		int32 Winner = -1;
		const TCHAR* Error = nullptr;
	};
	// What ObserveRush has recorded for one team-0 force.
	struct FRushForce
	{
		bool bAttacking = false;
		bool bRetreating = false;
		bool bWithdrawn = false;
	};
	bool Start(ACommandGameState& State);
	bool StartDuel(ACommandGameState& State);
	bool StartMatch(ACommandGameState& State);
	bool FindMatchActors(ACommandGameState& State);
	bool SpawnAutopilot();
	void ConfigureTime();
	void TickDuel(float DeltaTime);
	void TickMatch(float DeltaTime, ACommandGameState& State);
	void DescribeMatch(ACommandGameState& State);
	void DescribeRegions(ACommandGameState& State);
	void Observe(ACommandGameState& State);
	void ObservePlans(ACommandGameState& State);
	void ObserveUnits();
	void ObserveBuildings(ACommandGameState& State);
	void ObserveRegions(ACommandGameState& State);
	void ObserveDeposits(ACommandGameState& State);
	void ObserveHeadquarters(ACommandGameState& State);
	FOutcome ResolveOutcome(const ACommandGameState& State, double Time) const;
	// Rush scenario telemetry: when each team-0 force first lived and first attacked JEV's main.
	void ObserveRush(ACommandGameState& State);
	void Snapshot(ACommandGameState& State, double ScheduledTime);
	TSharedRef<FJsonObject> SnapshotTeam(ACommandGameState& State, int32 Slot) const;
	TSharedRef<FJsonObject> Event(const TCHAR* Kind, int32 Team = -1);
	bool Flush();
	void Finish(const TCHAR* Status, const TCHAR* Outcome, int32 Winner, const FString& Error = FString());
	TSharedPtr<FJsonObject> Report;
	TUniquePtr<FSimulationDuelRunner> DuelRunner;
	TWeakObjectPtr<ACommandPlayerState> HumanCommander;
	TWeakObjectPtr<AEnemyCommander> Autopilot;
	TMap<TWeakObjectPtr<AArmyUnit>, FObservedUnit> ObservedUnits;
	TMap<int32, int32> RegionOwners;
	// Team-0 forces seen in the rush scenario, and which of their transitions were already recorded.
	TMap<TWeakObjectPtr<AArmyGroup>, FRushForce> RushForces;
	int32 RushTargetRegion = INDEX_NONE;
	TSet<TWeakObjectPtr<ADepositSite>> Depleted;
	int32 ObservedPlanHistory = 0;
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
#endif
