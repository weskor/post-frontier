#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CoreMinimal.h"

class AActor;
class AArmyGroup;
class AArmyUnit;
class ACommandGameState;
class ACommandPlayerState;
class AHeadquarters;
class FJsonObject;
class FJsonValue;

namespace SimulationDuel
{
constexpr int32 DuelBudget = 120;
constexpr float DuelClearance = 1200.f;
constexpr float DuelSpacing = 160.f;
constexpr float DuelJitter = 40.f;
}

// Shared authoritative encounter runner for standalone measurement and worlds.
// Setup lives in MatchSimulationDuelRunner.cpp, the fight list and squads in MatchSimulationDuelFights.cpp,
// per-tick stepping in MatchSimulationDuelRunnerStep.cpp. One run is the ordered pair matrix of every combat
// definition (mirrors included), then the support composition fights.
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
		int32 Shield = 0;
		int32 Cost = 0;
		uint32 Attacks = 0;
	};
	// Count units of one definition in a squad; a squad spends at most the duel budget in whole units.
	struct FSquadPart
	{
		int32 Definition = INDEX_NONE;
		int32 Count = 0;
	};
	// One fight: a squad per side. Pairs are single-definition squads; a composition is a support scenario
	// whose `Subject` squad stands on `SubjectSide` against the target squad on the other side.
	struct FFight
	{
		TArray<FSquadPart> Squads[2];
		FString Kind;
		FName Support, Partner, Target;
		int32 SubjectSide = INDEX_NONE;
		bool IsComposition() const { return SubjectSide != INDEX_NONE; }
	};
	struct FPausedActor
	{
		TWeakObjectPtr<AActor> Actor;
		bool bTickEnabled = false;
	};
	bool FindWallets(ACommandGameState& InState);
	bool CollectDefinitions(ACommandGameState& InState, TArray<TSharedPtr<FJsonValue>>& Rows);
	void BuildFights(const ACommandGameState& InState);
	void AddComposition(const ACommandGameState& InState, int32 Support, int32 Partner, int32 Target);
	void IsolateWorld(ACommandGameState& InState);
	bool FindGround();
	void RecordGeometry();
	bool StartPair();
	bool SpawnSide(int32 Side, const TArray<FSquadPart>& Squad, const FVector& Forward, const FVector& Across,
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
	TArray<FFight> Fights;
	TArray<TWeakObjectPtr<AArmyGroup>> Groups;
	TArray<FMember> Members;
	TArray<FPausedActor> PausedActors;
	TSharedRef<FJsonObject> Report;
	TSharedPtr<FJsonObject> Current;
	FRandomStream Random;
	FVector Center = FVector::ZeroVector;
	int32 FightIndex = 0;
	int32 SpawnFirstSide = 0;
	int32 Initial[2] = {};
	int32 Spent[2] = {};
	int32 Survivors[2] = {};
	int32 SurvivorPower[2] = {};
	int64 Damage[2] = {};
	int64 ShieldDamage[2] = {};
	int64 Attacks[2] = {};
	double Elapsed = 0.;
	double LastDamageElapsed = -1.; // Unarmed until the first attack, HP loss or shield loss.
	double StallTimeout = 30.;
	double TotalElapsed = 0.;
	bool bStarted = false;
	bool bComplete = false;
	FString Error;
};
#endif
