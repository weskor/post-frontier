#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "MatchTelemetryScenario.h"
#include "ArmyTestSetup.h"
#include "MatchTelemetry.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatchTelemetryTest, "CoopRTS.Telemetry.Match",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace MatchTelemetryScenarioTests
{
FMatchScenario::FMatchScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
FMatchScenario::~FMatchScenario() { Cleanup(); }

bool FMatchScenario::Update()
{
	UWorld* World = ArmyTestSetup::World();
	ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
	Track(State);
	if (FPlatformTime::Seconds() - Started > 90.)
		return Fail(TEXT("Telemetry world scenario exceeded 90 seconds"));
	ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
	ACommandPlayerState* Host = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
	if (!Host || Host->CommanderIndex < 0 || !ArmyTestSetup::MapReady(State)
		|| !IsValid(State->EnemyCommander) || !State->Content || ArmyTestSetup::GameSeconds(World) < 3.)
		return false;
	if ((Stage == 0 || Stage == 7 || Stage == 9 || Stage == 10) && State->MatchTelemetry->GetBattleSeconds() < 3.)
		return false;
	if (Stage == 0 && !ArmyTestSetup::NavigationReady(World))
		return false;
	if (Stage == 0)
		return Stage0(World, PC, State, Host);
	if (Stage == 1)
		return Stage1(World, PC, State);
	if (Stage == 2)
		return Stage2(World, PC, State);
	if (Stage == 3)
		return Stage3(World, State);
	if (Stage == 4)
		return Stage4(World, State);
	if (Stage == 5)
		return Stage5(World, State);
	if (Stage == 6)
		return Stage6(World, PC, State, Host);
	if (Stage == 7)
		return Stage7(World, PC, State);
	if (Stage == 8)
		return Stage8(PC, State);
	if (Stage == 9)
		return Stage9(World, State);
	if (Stage == 10)
		return Stage10(World, State);
	if (Stage == 11)
		return Stage11(World, PC, State);
	if (State->MatchResult == EMatchResult::Ongoing)
		return false;
	if (!ValidateFile(State, true) || !OriginalUnchanged())
		return true;
	Test->AddInfo(TEXT("Telemetry proof: UUID-only attribution with no online IDs/hostnames/names; exact accepted counts and connected-participation rates; late join, real logout, same opaque-ID reconnect and gaps, zero-action and zero-duration humans retained; pause exclusion, real HQ cause/location, terminal and travel abandonment idempotence, fresh seamless restarts. JSON evidence logged before test-only cleanup."));
	Cleanup();
	return true;
}

bool FMatchScenario::Stage0(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Host)
{
	if (!Setup(World, PC, State))
		return true;
	OriginalState = State;
	OriginalTelemetry = State->MatchTelemetry;
	OriginalId = State->MatchTelemetry->GetMatchId();
	HostName = Host->GetPlayerName();
	HostIndex = Host->CommanderIndex;
	if (!Check(!OriginalId.IsEmpty() && State->MatchTelemetry->GetOutputPath().IsEmpty(),
			TEXT("Fresh authority component has a match identity but no premature output")))
		return true;
	StageStarted = ArmyTestSetup::GameSeconds(World);
	Stage = 1;
	return false;
}

bool FMatchScenario::Stage1(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
{
	if (ArmyTestSetup::GameSeconds(World) - StageStarted < 1.1)
		return false;
	if (!Check(FCommandService::Pause(PC).IsAccepted(), TEXT("Real pause isolates simulation duration")))
		return true;
	PausedSeconds = State->MatchTelemetry->GetBattleSeconds();
	if (!Commands(World, PC, State))
		return true;
	FirstLeft = State->MatchTelemetry->GetBattleSeconds();
	Leave(World, Leaver.Get());
	ACommandPlayerController* Instant = Participant(World, State, 3, TEXT("Telemetry instant private name"));
	InstantJoined = State->MatchTelemetry->GetBattleSeconds();
	AArmyGroup* InstantForce = Instant ? ArmyTestSetup::SpawnGroup(World, Instant, 0,
											 ArmyTestSetup::FromFriendlyHQ(State, 1600.f, 650.f, 100.f))
									   : nullptr;
	if (!Check(InstantForce && FCommandService::IssueForceOrder(Instant->GetPlayerState<ACommandPlayerState>(), InstantForce, EForceVerb::MoveHold, ArmyTestSetup::CurrentRegion(InstantForce)).IsAccepted(),
			TEXT("A zero-duration connected human can make an accepted decision while paused")))
		return true;
	Leave(World, Instant);
	StageStarted = World->GetRealTimeSeconds();
	Stage = 2;
	return false;
}

bool FMatchScenario::Stage2(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
{
	if (World->GetRealTimeSeconds() - StageStarted < 1.1)
		return false;
	if (!Check(State->MatchTelemetry->GetBattleSeconds() == PausedSeconds,
			TEXT("Paused real time is excluded from battle and participation seconds"))
		|| !Check(FCommandService::Resume(PC).IsAccepted(), TEXT("Real match resumes before reconnect")))
		return true;
	StageStarted = ArmyTestSetup::GameSeconds(World);
	Stage = 3;
	return false;
}

bool FMatchScenario::Stage3(UWorld* World, ACommandGameState* State)
{
	if (ArmyTestSetup::GameSeconds(World) - StageStarted < 1.1)
		return false;
	Leaver = Participant(World, State, 1, TEXT("Telemetry reconnected private name"), PrivateOnlineId);
	Rejoined = State->MatchTelemetry->GetBattleSeconds();
	LeavingForce = Leaver.IsValid() ? ArmyTestSetup::SpawnGroup(World, Leaver.Get(), 0,
										  ArmyTestSetup::FromFriendlyHQ(State, 1300.f, 650.f, 100.f))
									: nullptr;
	if (!Check(LeavingForce.IsValid(), TEXT("Reconnected online identity owns a fresh real force")))
		return true;
	Freeze(LeavingForce.Get());
	if (!Check(FCommandService::IssueForceOrder(Leaver->GetPlayerState<ACommandPlayerState>(), LeavingForce.Get(),
				   EForceVerb::MoveHold, ArmyTestSetup::CurrentRegion(LeavingForce.Get()))
				   .IsAccepted(),
			TEXT("Reconnected human's accepted order belongs to the original participant")))
		return true;
	StageStarted = ArmyTestSetup::GameSeconds(World);
	Stage = 4;
	return false;
}

bool FMatchScenario::Stage4(UWorld* World, ACommandGameState* State)
{
	if (ArmyTestSetup::GameSeconds(World) - StageStarted < 1.1)
		return false;
	LastLeft = State->MatchTelemetry->GetBattleSeconds();
	Leave(World, Leaver.Get());
	State->EnemyHeadquarters->ReceiveAttack(State->EnemyHeadquarters->Health, FirstForce->GetUnits()[0]);
	Stage = 5;
	return false;
}

bool FMatchScenario::Stage5(UWorld* World, ACommandGameState* State)
{
	if (State->MatchResult == EMatchResult::Ongoing)
		return false;
	if (!Check(State->MatchResult == EMatchResult::Victory, TEXT("Real HQ damage triggers terminal Victory"))
		|| !ValidateFile(State, false))
		return true;
	OriginalPath = State->MatchTelemetry->GetOutputPath();
	OriginalSeconds = State->MatchTelemetry->GetBattleSeconds();
	OriginalTimestamp = IFileManager::Get().GetTimeStamp(*OriginalPath);
	if (!Check(FFileHelper::LoadFileToString(OriginalJson, *OriginalPath), TEXT("Terminal JSON snapshot loads")))
		return true;
	StageStarted = ArmyTestSetup::GameSeconds(World);
	Stage = 6;
	return false;
}

bool FMatchScenario::Stage6(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State, ACommandPlayerState* Host)
{
	if (ArmyTestSetup::GameSeconds(World) - StageStarted < 1.1)
		return false;
	State->SetMatchResult(EMatchResult::Victory);
	State->MatchTelemetry->FlushMatch();
	if (!Check(!FCommandService::IssueForceOrder(Host, FirstForce.Get(), EForceVerb::Retreat).IsAccepted()
				&& !FCommandService::PlaceBuilding(Host, ArmyTestSetup::WorkshopIndex, BuildLocation).IsAccepted()
				&& !FCommandService::Ping(PC, BuildLocation).IsAccepted(),
			TEXT("All tracked commands reject after match end")))
		return true;
	FString CurrentJson;
	if (!Check(FFileHelper::LoadFileToString(CurrentJson, *OriginalPath) && CurrentJson == OriginalJson
				&& IFileManager::Get().GetTimeStamp(*OriginalPath) == OriginalTimestamp
				&& State->MatchTelemetry->GetBattleSeconds() == OriginalSeconds,
			TEXT("Duplicate terminal calls and rejected commands neither rewrite JSON nor change duration")))
		return true;
	Leave(World, ZeroPlayer.Get());
	if (!Check(FCommandService::Restart(PC).IsAccepted(), TEXT("Real command service accepts seamless match restart")))
		return true;
	Stage = 7;
	return false;
}

bool FMatchScenario::Stage7(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
{
	if (State == OriginalState.Get())
		return false;
	if (!Check(State->MatchResult == EMatchResult::Ongoing && State->MatchTelemetry != OriginalTelemetry.Get()
				&& State->MatchTelemetry->GetMatchId() != OriginalId && State->MatchTelemetry->GetOutputPath().IsEmpty(),
			TEXT("Seamless restart creates fresh component, identity, duration and output state")))
		return true;
	RestartState = State;
	Isolate(World, State);
	if (!EndWithHQ(World, PC, State))
		return true;
	Stage = 8;
	return false;
}

bool FMatchScenario::Stage8(ACommandPlayerController* PC, ACommandGameState* State)
{
	if (State->MatchResult == EMatchResult::Ongoing)
		return false;
	if (!ValidateFile(State, true) || !OriginalUnchanged()
		|| !Check(FCommandService::Restart(PC).IsAccepted(), TEXT("Terminal restart reaches the abandonment fixture")))
		return true;
	Stage = 9;
	return false;
}

bool FMatchScenario::Stage9(UWorld* World, ACommandGameState* State)
{
	if (State == RestartState.Get())
		return false;
	Isolate(World, State);
	AbandonedState = State;
	AbandonedTelemetry = State->MatchTelemetry;
	AbandonedId = State->MatchTelemetry->GetMatchId();
	AbandonedPath = PathFor(State->MatchTelemetry);
	AbandonedMap = UWorld::RemovePIEPrefix(World->GetOutermost()->GetName());
	if (!Check(State->MatchResult == EMatchResult::Ongoing && State->MatchTelemetry->GetOutputPath().IsEmpty()
				&& World->ServerTravel(AbandonedMap + TEXT("?SeamlessTravel"), false),
			TEXT("Real seamless travel abandons an ongoing unflushed match")))
		return true;
	Stage = 10;
	return false;
}

bool FMatchScenario::Stage10(UWorld* World, ACommandGameState* State)
{
	if (State == AbandonedState.Get())
		return false;
	if (!ValidateAbandoned() || !OriginalUnchanged())
		return true;
	AbandonedTimestamp = IFileManager::Get().GetTimeStamp(*AbandonedPath);
	if (!Check(FFileHelper::LoadFileToString(AbandonedJson, *AbandonedPath), TEXT("Abandoned JSON snapshot loads")))
		return true;
	if (AbandonedTelemetry.IsValid())
		AbandonedTelemetry->FlushMatch();
	Isolate(World, State);
	StageStarted = ArmyTestSetup::GameSeconds(World);
	Stage = 11;
	return false;
}

bool FMatchScenario::Stage11(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
{
	if (ArmyTestSetup::GameSeconds(World) - StageStarted < 1.1)
		return false;
	FString CurrentJson;
	if (!Check(FFileHelper::LoadFileToString(CurrentJson, *AbandonedPath) && CurrentJson == AbandonedJson
				&& IFileManager::Get().GetTimeStamp(*AbandonedPath) == AbandonedTimestamp,
			TEXT("Travel abandonment is idempotent and never rewritten by later lifecycle/flush calls"))
		|| !EndWithHQ(World, PC, State))
		return true;
	Stage = 12;
	return false;
}
}

bool FMatchTelemetryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(MatchTelemetryScenarioTests::FMatchScenario(this));
	return true;
}

#endif
