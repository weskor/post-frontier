#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/JevPlanner.h"
#include "JevMemoTemplates.h"
#include "Rules/JevReleasePolicy.h"
#include "EnemyCommander.generated.h"

class ACommandBuilding;
class AArmyGroup;
class ACommandGameState;
class ACommandPlayerState;
struct FJevTurn;
struct FJevForceStep;

// The ticket the executor holds for one force between evaluations.
struct FJevCommittedForce
{
	TWeakObjectPtr<AArmyGroup> Force;
	JevPlanner::FPlan Plan;
	int32 TicketNumber = 0;
	bool bRecovering = false;
	bool bCommandsRejected = false;
};

// One free wave, as clients see it.
USTRUCT()
struct FJevWaveEvent
{
	GENERATED_BODY()
	// Release index (0 is v1.0).
	UPROPERTY()
	int32 Release = 0;
	UPROPERTY()
	float MatchSeconds = 0.f;
	// Power-equivalent the wave bought with, including the carry from earlier waves.
	UPROPERTY()
	int32 Budget = 0;
	UPROPERTY()
	int32 Units = 0;
	UPROPERTY()
	int32 Forces = 0;
	UPROPERTY()
	int32 TargetRegion = INDEX_NONE;
};

// JEV's release schedule as every peer reads it. Times are match seconds on the clock
// AEnemyCommander::GetMatchSeconds defines; a client's match time is
// GetServerWorldTimeSeconds() - ClockStartServerTime.
USTRUCT()
struct FJevReleaseState
{
	GENERATED_BODY()
	// The release in force (0 is v1.0) and the one that follows.
	UPROPERTY()
	int32 Current = 0;
	UPROPERTY()
	int32 Next = 1;
	UPROPERTY()
	float NextAt = 0.f;
	// True once the next release is within JevRelease::TimelineLeadSeconds.
	UPROPERTY()
	bool bNextShown = false;
	UPROPERTY()
	float ClockStartServerTime = 0.f;
	// Waves launched this match, and the most recent ones (oldest first).
	UPROPERTY()
	int32 WaveCount = 0;
	UPROPERTY()
	TArray<FJevWaveEvent> Waves;
};

// Executor for JEV: EvaluatePlan summarises the match for the pure planner
// (Rules/JevPlanner), turns its choices into commands and publishes them.
// The steps live in EnemyCommanderWorld/Economy/Execute/Publish.cpp; the release schedule and
// its free waves in EnemyCommanderRelease/Wave.cpp.
UCLASS()
class COOPRTS_API AEnemyCommander : public AActor
{
	GENERATED_BODY()
public:
	AEnemyCommander();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void Tick(float DeltaSeconds) override;
	void EvaluatePlan();
	// The one source of match time for JEV's schedule: seconds since match start, frozen by pause.
	float GetMatchSeconds() const;
	static constexpr int32 MaxPublishedWaves = 8;
	// Release schedule and wave events for clients (team 5 only).
	UPROPERTY(Replicated)
	FJevReleaseState Release;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Moves match time forward without waiting, for world tests.
	void SkipClock(float Seconds) { ClockSkew += Seconds; }
#endif
	// Team 0 is created only by an explicit autopilot fixture; normal play creates team 5.
	// Replicated so a client can tell the autopilot's empty release state from JEV's.
	UPROPERTY(Replicated)
	int32 TeamIndex = 5;
	UPROPERTY()
	TObjectPtr<ACommandPlayerState> Commander;
private:
	bool BeginTurn(FJevTurn& Turn);
	void ExecuteForces(FJevTurn& Turn);
	void ExecuteForce(FJevTurn& Turn, AArmyGroup* Force);
	// Orders a wave force, or a force joining the wave, to Attack Target under a fresh ticket.
	void ExecuteWaveForce(FJevTurn& Turn, AArmyGroup* Force, int32 Target, bool bJoining);
	void Commit(FJevTurn& Turn, FJevForceStep& Step);
	void Publish(FJevTurn& Turn, const FJevForceStep& Step);
	// Publishes the schedule; true when a release is due and its wave not yet launched.
	bool TickRelease();
	void AdvanceReleases(FJevTurn& Turn);
	void LaunchWave(FJevTurn& Turn, int32 ReleaseIndex);
	void RecordWave(const FJevWaveEvent& Event);
	TArray<FJevCommittedForce, TInlineAllocator<8>> CommittedForces;
	// Free forces this commander launched; they never refill, so they fight on.
	TArray<TWeakObjectPtr<AArmyGroup>, TInlineAllocator<8>> WaveForces;
	bool IsWaveForce(const AArmyGroup* Force) const;
	FJevMemoTemplates MemoTemplates;
	bool bMemoLoadAttempted = false;
	bool bMemosLoaded = false;
	int32 NextTicketNumber = 1;
	float EvaluateElapsed = 0.f;
	// Power-equivalent left over from earlier waves, added to the next budget.
	int32 WaveCarry = 0;
	// The newest release whose wave has launched.
	int32 LaunchedUpTo = 0;
	float ReleaseRetryElapsed = 0.f;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	float ClockSkew = 0.f;
#endif
};
