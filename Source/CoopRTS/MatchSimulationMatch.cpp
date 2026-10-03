#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "ArenaBounds.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/MatchContent.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
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
	const double Time = GetWorld()->GetTimeSeconds() - StartWorldTime;
	if (State.MatchResult != EMatchResult::Ongoing)
	{
		const int32 Winner = State.MatchResult == EMatchResult::Victory ? 0 : 5;
		if ((Winner == 0 ? State.EnemyHeadquarters->Health : State.FriendlyHeadquarters->Health) > 0)
		{
			Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Terminal match result without destroyed HQ"));
			return;
		}
		Snapshot(State, Time);
		Finish(TEXT("complete"), TEXT("hq_destroyed"), Winner);
		return;
	}
	if (Time >= FSimulationSettings::Get().TimeCap)
	{
		// A same-frame HQ death must be an outcome, not a time-cap draw, even before GameMode's tick.
		Snapshot(State, Time);
		const int32 Winner = State.FriendlyHeadquarters->Health <= 0 ? 5 : State.EnemyHeadquarters->Health <= 0 ? 0
																												: -1;
		Finish(TEXT("complete"), Winner == -1 ? TEXT("time_cap") : TEXT("hq_destroyed"), Winner);
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
#endif
