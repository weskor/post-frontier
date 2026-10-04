#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "MatchSimulationSubsystem.h"

#include "CommandGameState.h"
#include "SimulationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "HAL/PlatformMisc.h"
#include "HAL/PlatformTime.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/EngineVersion.h"

FMatchSimulation::FMatchSimulation(UWorld* InWorld)
	: World(InWorld)
{
	const FSimulationSettings& Settings = FSimulationSettings::Get();
	FMath::RandInit(Settings.Seed);
	FMath::SRandInit(Settings.Seed);
	StartWallTime = FPlatformTime::Seconds();
	Report = MakeShared<FJsonObject>();
	Report->SetNumberField(TEXT("schema_version"), 1);
	if (Settings.bDuel)
		Report->SetStringField(TEXT("mode"), TEXT("duel"));
	Report->SetStringField(TEXT("status"), TEXT("initializing"));
	Report->SetStringField(TEXT("outcome"), TEXT("none"));
	Report->SetField(TEXT("winner"), MakeShared<FJsonValueNull>());
	Report->SetNumberField(TEXT("seed"), Settings.Seed);
	Report->SetStringField(TEXT("engine_version"), FEngineVersion::Current().ToString());
	Report->SetStringField(TEXT("command_line"), FCommandLine::Get());
	Report->SetStringField(TEXT("map"), UWorld::RemovePIEPrefix(GetWorld()->GetOutermost()->GetName()));
	Report->SetNumberField(TEXT("time_cap_seconds"), Settings.TimeCap);
	Report->SetNumberField(TEXT("requested_dilation"), Settings.Dilation);
	Report->SetNumberField(TEXT("snapshot_interval_seconds"), 30);
	Report->SetArrayField(TEXT("snapshots"), TArray<TSharedPtr<FJsonValue>>{});
	Report->SetArrayField(TEXT("events"), TArray<TSharedPtr<FJsonValue>>{});
	const TSharedRef<FJsonObject> Economy = MakeShared<FJsonObject>();
	Economy->SetNumberField(TEXT("human_baseline"), Settings.HumanBaselineIncome);
	Economy->SetNumberField(TEXT("jev_baseline"), Settings.JevBaselineIncome);
	Economy->SetNumberField(TEXT("normal_rate"), Settings.NormalRate);
	Economy->SetNumberField(TEXT("rich_rate"), Settings.RichRate);
	Economy->SetNumberField(TEXT("normal_amount"), Settings.NormalAmount);
	Economy->SetNumberField(TEXT("rich_amount"), Settings.RichAmount);
	Report->SetObjectField(TEXT("economy"), Economy);
}

FMatchSimulation::~FMatchSimulation()
{
	if (Report.IsValid() && !bFinished)
	{
		Report->SetStringField(TEXT("status"), TEXT("interrupted"));
		Report->SetStringField(TEXT("error"), TEXT("World ended before a natural match result"));
		Flush();
	}
	DuelRunner.Reset();
}

TStatId FMatchSimulation::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(FMatchSimulation, STATGROUP_Tickables);
}

bool FMatchSimulation::Start(ACommandGameState& State)
{
	if (GetWorld()->GetNetMode() != NM_Standalone)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Autopilot simulation requires standalone, not a network world"));
		return false;
	}
	if (!FSimulationSettings::Get().Error.IsEmpty())
	{
		Finish(TEXT("failed"), TEXT("none"), -1, FSimulationSettings::Get().Error);
		return false;
	}
	return FSimulationSettings::Get().bDuel ? StartDuel(State) : StartMatch(State);
}

void FMatchSimulation::ConfigureTime()
{
	AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings();
	WorldSettings->SetAllowTimeDilation(true);
	WorldSettings->SetTimeDilation(FSimulationSettings::Get().Dilation);
	const float EffectiveDilation = WorldSettings->GetEffectiveTimeDilation();
	// Combat and production intentionally do at most one action per actor tick.
	// Increase virtual tick frequency with dilation instead of giving them coarse game deltas.
	const double FixedDelta = 1. / (60. * EffectiveDilation);
	WorldSettings->MinUndilatedFrameTime = 0.f;
	WorldSettings->MaxUndilatedFrameTime = 1.f;
	FApp::SetFixedDeltaTime(FixedDelta);
	FApp::SetUseFixedTimeStep(true);
	Report->SetNumberField(TEXT("effective_dilation"), EffectiveDilation);
	Report->SetNumberField(TEXT("fixed_undilated_delta_seconds"), FixedDelta);
	Report->SetNumberField(TEXT("target_game_delta_seconds"), 1. / 60.);
	Report->SetStringField(TEXT("status"), TEXT("running"));
}

void FMatchSimulation::Finish(const TCHAR* Status, const TCHAR* Outcome, int32 Winner, const FString& Error)
{
	if (bFinished)
		return;
	bFinished = true;
	Report->SetStringField(TEXT("status"), Status);
	Report->SetStringField(TEXT("outcome"), Outcome);
	if (Winner == 0 || Winner == 5)
		Report->SetNumberField(TEXT("winner"), Winner);
	else
		Report->SetField(TEXT("winner"), MakeShared<FJsonValueNull>());
	if (!Error.IsEmpty())
		Report->SetStringField(TEXT("error"), Error);
	Event(DuelRunner ? TEXT("matrix_finished") : TEXT("match_finished"))->SetStringField(TEXT("outcome"), Outcome);
	const bool bWritten = Flush();
	const bool bSuccess = FCString::Strcmp(Status, TEXT("complete")) == 0 && bWritten;
	UE_LOG(LogTemp, Display, TEXT("Simulation finished status=%s outcome=%s written=%d error=%s"), Status, Outcome, bWritten, *Error);
	FPlatformMisc::RequestExitWithStatus(false, bSuccess ? 0 : 1, TEXT("MatchSimulation"));
}

void FMatchSimulation::Tick(float DeltaTime)
{
	if (bFinished || !GetWorld()->HasBegunPlay())
		return;
	ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
	{
		Finish(TEXT("failed"), TEXT("none"), -1, TEXT("Requested world is not a command match"));
		return;
	}
	if (!bStarted)
	{
		// Let the chosen map's navigation finish its initial asynchronous build.
		if (FSimulationSettings::Get().bDuel && GetWorld()->GetTimeSeconds() < 3.f)
			return;
		Start(*State);
		return;
	}
	if (DuelRunner)
		TickDuel(DeltaTime);
	else
		TickMatch(DeltaTime, *State);
}

namespace CoopRTSMatchSimulation
{
namespace
{
TMap<UWorld*, TUniquePtr<FMatchSimulation>> Matches;
FDelegateHandle InitializeHandle, CleanupHandle;

void InitializeWorld(UWorld* World, const UWorld::InitializationValues)
{
	if (World->WorldType == EWorldType::Game && FSimulationSettings::Get().bEnabled)
		Matches.Add(World, MakeUnique<FMatchSimulation>(World));
}

void CleanupWorld(UWorld* World, bool, bool)
{
	Matches.Remove(World);
}
}

void Start()
{
	InitializeHandle = FWorldDelegates::OnPreWorldInitialization.AddStatic(&InitializeWorld);
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddStatic(&CleanupWorld);
}

void Stop()
{
	FWorldDelegates::OnPreWorldInitialization.Remove(InitializeHandle);
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	Matches.Empty();
}
}
#endif
