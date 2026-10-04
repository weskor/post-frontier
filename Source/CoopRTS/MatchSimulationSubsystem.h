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

#include "MatchSimulationDuelRunner.h"
#include "Rules/HqHoldPolicy.h"

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
	friend struct FMatchSimulationTestAccess;
	UWorld* GetWorld() const { return World; }
	UWorld* World;
	struct FObservedUnit
	{
		int32 TeamSlot = 0;
		int32 Health = 0;
		uint32 Attacks = 0;
	};
	// How a tick's state ends the match, if it does. The one place that interprets outcomes: by the HQ lifecycle,
	// never by hit points. A battle ends on a completed hold (`hold_completed`); an offline HQ at the time cap is
	// a censored draw (`time_cap`).
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
	HqHoldPolicy::EPhase PreviousHqPhase[2] = { HqHoldPolicy::EPhase::Online, HqHoldPolicy::EPhase::Online };
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
