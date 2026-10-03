#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "SimulationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"

bool FMatchSimulation::StartDuel(ACommandGameState& State)
{
	DuelRunner = MakeUnique<FSimulationDuelRunner>();
	const TSharedPtr<FJsonObject> Metadata = Report;
	if (!DuelRunner->Start(State, FSimulationSettings::Get().Seed))
	{
		Finish(TEXT("failed"), TEXT("none"), -1, DuelRunner->GetError());
		return false;
	}
	Report = DuelRunner->GetReport();
	Report->Values.Append(Metadata->Values);
	StartWorldTime = GetWorld()->GetTimeSeconds();
	bStarted = true;
	ConfigureTime();
	Event(TEXT("matrix_started"));
	if (!Flush())
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot write initial duel telemetry"));
	return !bFinished;
}

void FMatchSimulation::TickDuel(float DeltaTime)
{
	MaxGameDelta = FMath::Max(MaxGameDelta, DeltaTime);
	static const FString DuelsField(TEXT("duels"));
	const int32 CompletedBefore = Report->GetArrayField(DuelsField).Num();
	DuelRunner->Tick(DeltaTime, FSimulationSettings::Get().TimeCap);
	if (!DuelRunner->GetError().IsEmpty())
	{
		const FString Outcome = Report->GetStringField(TEXT("outcome"));
		Finish(TEXT("failed"), *Outcome, -1, DuelRunner->GetError());
		return;
	}
	if (DuelRunner->IsComplete())
	{
		Finish(TEXT("complete"), TEXT("matrix_complete"), -1);
		return;
	}
	const double Time = GetWorld()->GetTimeSeconds() - StartWorldTime;
	if (Report->GetArrayField(DuelsField).Num() != CompletedBefore || Time >= NextSnapshot)
	{
		NextSnapshot = (FMath::FloorToDouble(Time / 30.) + 1.) * 30.;
		if (!Flush())
			Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Cannot persist duel checkpoint"));
	}
}
#endif
