#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "SimulationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "EngineUtils.h"

bool FMatchSimulation::StartMatch(ACommandGameState& State)
{
	if (!FindMatchActors(State) || !SpawnAutopilot())
		return false;
	StartWorldTime = GetWorld()->GetTimeSeconds();
	// Planning ran before this tick on the game state's harness net; the match clock starts after it.
	Report->SetNumberField(TEXT("planning_seconds"), State.GetPlanningSeconds());
	if (FSimulationSettings::Get().Scenario == ESimulationScenario::Rush)
	{
		const AMapRegion* Target = State.FindRegionAt(State.EnemyHeadquarters->GetActorLocation());
		RushTargetRegion = Target ? Target->RegionIndex : INDEX_NONE;
		Report->SetNumberField(TEXT("rush_target_region"), RushTargetRegion);
	}
	bStarted = true;
	ConfigureTime();
	DescribeMatch(State);
	Event(TEXT("match_started"));
	Observe(State);
	Snapshot(State, 0.);
	if (!Flush())
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot write initial telemetry"));
	return !bFinished;
}

bool FMatchSimulation::FindMatchActors(ACommandGameState& State)
{
	if (!IsValid(State.Content) || !IsValid(State.FriendlyHeadquarters) || !IsValid(State.EnemyHeadquarters)
		|| !IsValid(State.Arena) || State.Regions.IsEmpty() || State.Deposits.IsEmpty() || !IsValid(State.EnemyCommander))
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Map lacks initialized match content, arena, HQs, regions, deposits or enemy wallet"));
		return false;
	}
	for (APlayerState* Player : State.PlayerArray)
		if (ACommandPlayerState* Commander = Cast<ACommandPlayerState>(Player))
			if (Commander->TeamIndex == 0 && Commander->CommanderIndex >= 0)
			{
				if (HumanCommander.IsValid())
				{
					Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Simulation requires exactly one team-0 commander"));
					return false;
				}
				HumanCommander = Commander;
			}
	if (!HumanCommander.IsValid())
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Standalone local commander was not initialized"));
		return false;
	}
	int32 EnemyPlanners = 0;
	for (TActorIterator<AEnemyCommander> It(GetWorld()); It; ++It)
	{
		if (It->TeamIndex == 5)
			++EnemyPlanners;
		else
		{
			Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Map already contains an autopilot planner"));
			return false;
		}
	}
	if (EnemyPlanners != 1)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Simulation requires exactly one normal enemy planner"));
		return false;
	}
	return true;
}

bool FMatchSimulation::SpawnAutopilot()
{
	const FTransform Transform = FTransform::Identity;
	AEnemyCommander* Planner = GetWorld()->SpawnActorDeferred<AEnemyCommander>(AEnemyCommander::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Planner)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Could not spawn team-0 planner"));
		return false;
	}
	Planner->TeamIndex = 0;
	Planner->Commander = HumanCommander.Get();
	Planner->bRushScenario = FSimulationSettings::Get().Scenario == ESimulationScenario::Rush;
	Planner->FinishSpawning(Transform);
	Autopilot = Planner;
	return true;
}

void FMatchSimulation::TickMatch(float DeltaTime, ACommandGameState& State)
{
	if (!HumanCommander.IsValid() || !Autopilot.IsValid() || !IsValid(State.EnemyCommander)
		|| !IsValid(State.FriendlyHeadquarters) || !IsValid(State.EnemyHeadquarters))
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Required match actor disappeared before completion"));
		return;
	}
	MaxGameDelta = FMath::Max(MaxGameDelta, DeltaTime);
	Observe(State);
	ObserveRush(State);
	const double Time = GetWorld()->GetTimeSeconds() - StartWorldTime;
	const FOutcome Outcome = ResolveOutcome(State, Time);
	if (Outcome.Error)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, Outcome.Error);
		return;
	}
	if (Outcome.bEnded)
	{
		Snapshot(State, Time);
		Finish(TEXT("complete"), Outcome.Kind, Outcome.Winner);
		return;
	}
	if (Time >= NextSnapshot)
	{
		Snapshot(State, NextSnapshot);
		// Never fabricate past states if a coarse frame skipped a sampling boundary.
		NextSnapshot = (FMath::FloorToDouble(Time / 30.) + 1.) * 30.;
		if (!Flush())
			Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot persist telemetry checkpoint"));
	}
}

FMatchSimulation::FOutcome FMatchSimulation::ResolveOutcome(const ACommandGameState& State, double Time) const
{
	FOutcome Outcome;
	// A side is out of the battle only when its HQ is lost (a completed hold). An offline HQ at 0 HP is still in it.
	const bool bFriendlyLost = !State.FriendlyHeadquarters->IsAlive();
	const bool bEnemyLost = !State.EnemyHeadquarters->IsAlive();
	if (State.MatchResult != EMatchResult::Ongoing)
	{
		const int32 Winner = State.MatchResult == EMatchResult::Victory ? 0 : 5;
		if (!(Winner == 0 ? bEnemyLost : bFriendlyLost))
		{
			Outcome.Error = TEXT("Terminal match result without a lost HQ");
			return Outcome;
		}
		Outcome.bEnded = true;
		Outcome.Kind = TEXT("hold_completed");
		Outcome.Winner = Winner;
		return Outcome;
	}
	if (Time < FSimulationSettings::Get().TimeCap)
		return Outcome;
	// A hold completed in the cap's own frame must be an outcome, not a time-cap draw, even before GameMode's tick.
	// Team 5 wins a simultaneous loss, as in the game's outcome policy. An HQ still offline at the cap is
	// undecided: a censored draw.
	Outcome.bEnded = true;
	Outcome.Winner = bFriendlyLost ? 5 : bEnemyLost ? 0
													: -1;
	Outcome.Kind = Outcome.Winner == -1 ? TEXT("time_cap") : TEXT("hold_completed");
	return Outcome;
}

void FMatchSimulation::ObserveRush(ACommandGameState& State)
{
	if (FSimulationSettings::Get().Scenario != ESimulationScenario::Rush)
		return;
	for (TActorIterator<AArmyGroup> It(GetWorld()); It; ++It)
	{
		if (It->GetTeamIndex() != 0 || It->GetAliveCount() == 0)
			continue;
		const auto Record = [&](const TCHAR* Kind) {
			const TSharedRef<FJsonObject> Row = Event(Kind, 0);
			Row->SetStringField(TEXT("id"), It->GetName());
			Row->SetNumberField(TEXT("force"), It->ForceNumber);
			Row->SetNumberField(TEXT("target_region"), It->TargetRegionIndex);
			Row->SetNumberField(TEXT("status"), static_cast<uint8>(It->Status));
			// The size at which a withdrawal resumes, not a count of resumes.
			Row->SetNumberField(TEXT("resume_threshold"), It->ResumeCount);
		};
		const TWeakObjectPtr<AArmyGroup> Key(*It);
		FRushForce* Rush = RushForces.Find(Key);
		if (!Rush)
		{
			Rush = &RushForces.Add(Key, FRushForce{});
			Record(TEXT("rush_force_seen"));
		}
		// The rush's objective is JEV's main, or a Failover Node (a structure attack) while one guards its HQ.
		const bool bObjective = It->TargetRegionIndex == RushTargetRegion || IsValid(It->TargetStructure);
		if (!Rush->bAttacking && It->Verb == EForceVerb::Attack && bObjective)
		{
			Rush->bAttacking = true;
			Record(TEXT("rush_force_attacking"));
		}
		// A rush never issues Retreat: that event is a defect. Withdrawing and refilling are the force's own
		// casualty withdrawal under the standing Attack; it resumes by leaving them with the verb still Attack
		// (ResumeCount is the resume threshold, which does not change on a resume).
		const bool bRetreating = It->Verb == EForceVerb::Retreat;
		if (bRetreating && !Rush->bRetreating)
			Record(TEXT("rush_force_retreat"));
		Rush->bRetreating = bRetreating;
		const bool bWithdrawn = It->Status == EForceStatus::Withdrawing || It->Status == EForceStatus::Refilling;
		if (bWithdrawn && !Rush->bWithdrawn)
			Record(TEXT("rush_force_withdrawing"));
		if (!bWithdrawn && Rush->bWithdrawn && It->Verb == EForceVerb::Attack)
			Record(TEXT("rush_force_resumed"));
		Rush->bWithdrawn = bWithdrawn;
	}
}
#endif
