#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/JevPlanner.h"
#include "JevMemoTemplates.h"
#include "Rules/JevReleasePolicy.h"
#include "Rules/JevThreatPolicy.h"
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
	// Power-equivalent the wave bought with, including the carry from earlier waves. A cut force: what its units cost.
	UPROPERTY()
	int32 Budget = 0;
	UPROPERTY()
	int32 Units = 0;
	UPROPERTY()
	int32 Forces = 0;
	UPROPERTY()
	int32 TargetRegion = INDEX_NONE;
	// An emergency wave (HqHoldPolicy), not a scheduled release's; it does not count in FJevReleaseState::WaveCount.
	UPROPERTY()
	bool bEmergency = false;
	// A Split-Brain Cut force (JevThreat), not a scheduled release's wave; it does not count in FJevReleaseState::WaveCount.
	UPROPERTY()
	bool bCut = false;
};

// One Split-Brain Cut plan between its publication (JevThreat::LeadSeconds before the release) and the launch of its
// force. Once the force exists its plan is an ordinary one (ACommandGameState::EnemyPlans).
USTRUCT()
struct FJevCutPlan
{
	GENERATED_BODY()
	UPROPERTY()
	int32 Ticket = 0;
	UPROPERTY()
	int32 Source = INDEX_NONE;
	UPROPERTY()
	int32 Target = INDEX_NONE;
	UPROPERTY()
	int32 SizeBand = 2;
	// Seconds from EtaIssuedAt (a server time) to the force's arrival: the lead before launch plus the march.
	UPROPERTY()
	float EtaSeconds = 0.f;
	UPROPERTY()
	float EtaIssuedAt = 0.f;
	UPROPERTY()
	FString Memo;
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
	// The armor class the humans field most of as of JEV's last evaluation: what a countering wave (v1.2) buys against.
	// Unset with no human unit alive. The HUD tags the v1.2 release with it.
	UPROPERTY()
	EArmorClass CounterArmor = EArmorClass::Unset;
	// Release waves launched this match, and the most recent waves of either kind (oldest first).
	UPROPERTY()
	int32 WaveCount = 0;
	UPROPERTY()
	TArray<FJevWaveEvent> Waves;
	// The published Split-Brain Cut plans, from JevThreat::PublishTime until the forces launch.
	UPROPERTY()
	TArray<FJevCutPlan> Cuts;
	// The forces a Split-Brain Cut launched: their live plans keep the threat's name on the timeline and badges.
	UPROPERTY()
	TArray<TObjectPtr<AArmyGroup>> CutForces;
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
	// The emergency wave of decision H3: a free wave at the current release's budget, launched at JEV's main
	// on the next evaluation, which this call makes due at once. Authority only.
	void RequestEmergencyWave()
	{
		bEmergencyWavePending = true;
		EvaluateElapsed = 2.f;
	}
	static constexpr int32 MaxPublishedWaves = 8;
	// Release schedule and wave events for clients (team 5 only).
	UPROPERTY(Replicated)
	FJevReleaseState Release;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Moves match time forward without waiting, for world tests.
	void SkipClock(float Seconds) { ClockSkew += Seconds; }
	// The "rush" simulation scenario, honoured by the team-0 autopilot only: every force attacks
	// the opposing main at spawn and after every refill, and nothing but casualty withdrawal retreats.
	bool bRushScenario = false;
#endif
	// Team 0 is created only by an explicit autopilot fixture; normal play creates team 5.
	// Replicated so a client can tell the autopilot's empty release state from JEV's.
	UPROPERTY(Replicated)
	int32 TeamIndex = 5;
	UPROPERTY()
	TObjectPtr<ACommandPlayerState> Commander;
private:
	bool BeginTurn(FJevTurn& Turn);
	bool IsRushAutopilot() const
	{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
		return bRushScenario && TeamIndex == 0;
#else
		return false;
#endif
	}
	void ExecuteForces(FJevTurn& Turn);
	void ExecuteForce(FJevTurn& Turn, AArmyGroup* Force);
	// Orders a wave force, or a force joining the wave, to Attack Target under a fresh ticket.
	void ExecuteWaveForce(FJevTurn& Turn, AArmyGroup* Force, int32 Target, bool bJoining);
	void Commit(FJevTurn& Turn, FJevForceStep& Step);
	void Publish(FJevTurn& Turn, const FJevForceStep& Step);
	// Publishes the schedule; true when a release is due and its wave not yet launched.
	bool TickRelease();
	void AdvanceReleases(FJevTurn& Turn);
	// A release wave. An emergency wave (decision H3) buys one wave at the release's budget without the release
	// carry, never calls the other forces to join, and attacks the humans standing in JEV's own main.
	void LaunchWave(FJevTurn& Turn, int32 ReleaseIndex, bool bEmergency = false);
	// The emergency wave at the current release's budget (v1.1's before 120 s).
	void LaunchEmergencyWave(FJevTurn& Turn);
	void RecordWave(const FJevWaveEvent& Event);
	// Split-Brain Cut (JevThreat): AdvanceThreat steps its stage from the match clock, PublishThreat chooses the pair, buys
	// each force and publishes its plan JevThreat::LeadSeconds early, and LaunchThreat spawns and orders the forces.
	void AdvanceThreat(FJevTurn& Turn);
	void PublishThreat(FJevTurn& Turn);
	void LaunchThreat(FJevTurn& Turn);
	struct FPendingCut
	{
		int32 Target = INDEX_NONE;
		// The Power-equivalent the force's units cost; the budget cap only limits it.
		int32 Spent = 0;
		// Catalogue unit index of each unit bought.
		TArray<int32> Roster;
	};
	JevThreat::EStage ThreatStage = JevThreat::EStage::Waiting;
	TArray<FPendingCut, TInlineAllocator<2>> PendingCuts;
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
	bool bEmergencyWavePending = false;
	float ReleaseRetryElapsed = 0.f;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	float ClockSkew = 0.f;
#endif
};
