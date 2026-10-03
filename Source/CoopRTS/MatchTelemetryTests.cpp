#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CommandGameMode.h"
#include "MatchTelemetry.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatchTelemetryTest, "CoopRTS.Telemetry.Match",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace MatchTelemetryScenarioTests
{
// Fresh standalone match; only explicit fixtures act. The test exercises the
// public command service, real logout, real HQ outcome and seamless restart.
class FMatchScenario : public IAutomationLatentCommand
{
public:
	explicit FMatchScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 90.)
			return Fail(TEXT("Telemetry world scenario exceeded 90 seconds"));
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandPlayerState* Host = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!Host || Host->CommanderIndex < 0 || !ArmyTestSetup::MapReady(State)
			|| !IsValid(State->EnemyCommander) || !State->Content || World->GetTimeSeconds() < 3.f)
			return false;
		if ((Stage == 0 || Stage == 4) && State->MatchTelemetry->GetBattleSeconds() < 3.)
			return false;
		if (Stage == 0)
		{
			if (!Setup(World, PC, State))
				return true;
			OriginalState = State;
			OriginalTelemetry = State->MatchTelemetry;
			OriginalId = State->MatchTelemetry->GetMatchId();
			HostName = Host->GetPlayerName();
			HostIndex = Host->CommanderIndex;
			if (!Check(!OriginalId.IsEmpty() && State->MatchTelemetry->GetOutputPath().IsEmpty(),
					TEXT("Fresh authority component has a match identity but no premature output"))
				|| !Check(FCommandService::Pause(PC).IsAccepted(), TEXT("Real pause isolates simulation duration")))
				return true;
			PausedSeconds = State->MatchTelemetry->GetBattleSeconds();
			if (!Commands(World, PC, State))
				return true;
			StageStarted = Now;
			Stage = 1;
			return false;
		}
		if (Stage == 1)
		{
			if (Now - StageStarted < 1.1)
				return false;
			if (!Check(State->MatchTelemetry->GetBattleSeconds() == PausedSeconds,
					TEXT("Paused wall-clock time is excluded from battle simulation seconds"))
				|| !Check(FCommandService::Resume(PC).IsAccepted(), TEXT("Real match resumes before terminal outcome")))
				return true;
			State->EnemyHeadquarters->ReceiveAttack(State->EnemyHeadquarters->Health, FirstForce->GetUnits()[0]);
			Stage = 2;
			return false;
		}
		if (Stage == 2)
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
			StageStarted = Now;
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			if (Now - StageStarted < 1.1)
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
			ACommandGameMode* Mode = World->GetAuthGameMode<ACommandGameMode>();
			ACommandPlayerState* ZeroState = ZeroPlayer->GetPlayerState<ACommandPlayerState>();
			Mode->Logout(ZeroPlayer.Get());
			ZeroPlayer->Destroy();
			if (IsValid(ZeroState))
				ZeroState->Destroy();
			if (!Check(FCommandService::Restart(PC).IsAccepted(), TEXT("Real command service accepts seamless match restart")))
				return true;
			Stage = 4;
			return false;
		}
		if (Stage == 4)
		{
			if (State == OriginalState.Get())
				return false;
			if (!Check(State->MatchResult == EMatchResult::Ongoing && State->MatchTelemetry != OriginalTelemetry.Get()
						&& State->MatchTelemetry->GetMatchId() != OriginalId && State->MatchTelemetry->GetOutputPath().IsEmpty(),
					TEXT("Seamless restart creates fresh component, identity, duration and output state")))
				return true;
			Isolate(World, State);
			AArmyGroup* Attacker = ArmyTestSetup::SpawnGroup(World, PC, 0,
				ArmyTestSetup::FromFriendlyHQ(State, 700.f, 600.f, 100.f));
			if (!Check(Attacker && !Attacker->GetUnits().IsEmpty(), TEXT("Fresh match has an explicit unpaid terminal attacker")))
				return true;
			Freeze(Attacker);
			State->EnemyHeadquarters->ReceiveAttack(State->EnemyHeadquarters->Health, Attacker->GetUnits()[0]);
			Stage = 5;
			return false;
		}
		if (State->MatchResult == EMatchResult::Ongoing)
			return false;
		if (!ValidateFile(State, true))
			return true;
		FString RetainedJson;
		if (!Check(State->MatchTelemetry->GetOutputPath() != OriginalPath
					&& FFileHelper::LoadFileToString(RetainedJson, *OriginalPath) && RetainedJson == OriginalJson,
				TEXT("Fresh terminal output uses a unique filename and retains the previous match unchanged")))
			return true;
		Test->AddInfo(TEXT("Telemetry proof: accepted multi/single-force orders count once, rejected/AI/configuration excluded; placement/ping exact counts, zero-action and disconnected humans retained; finite full-simulation-minute rates, pause exclusion, real HQ location/cause, terminal idempotence and fresh seamless restart counters."));
		return true;
	}

private:
	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
		{
			Test->AddError(Message);
			ResumeAfterFailure();
		}
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		ResumeAfterFailure();
		return true;
	}
	void ResumeAfterFailure()
	{
		UWorld* World = ArmyTestSetup::World();
		if (World && World->IsPaused())
			FCommandService::Resume(ArmyTestSetup::Controller(World));
	}
	void Isolate(UWorld* World, ACommandGameState* State)
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building))
				Building->SetActorTickEnabled(false);
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		State->bVerificationIncomePaused = true;
	}
	void Freeze(AArmyGroup* Force)
	{
		Force->SetActorTickEnabled(false);
		for (AArmyUnit* Unit : Force->GetUnits())
			Unit->SetActorTickEnabled(false);
	}
	ACommandPlayerController* Participant(UWorld* World, ACommandGameState* State, int32 Index, const TCHAR* Name)
	{
		ACommandPlayerController* PC = World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* Player = World->SpawnActor<ACommandPlayerState>();
		if (!Check(PC && Player, TEXT("Human roster fixture spawns")))
			return nullptr;
		PC->SetPlayerState(Player);
		Player->SetOwner(PC);
		Player->CommanderIndex = Index;
		Player->SetPlayerName(Name);
		State->AddPlayerState(Player);
		return PC;
	}
	bool Setup(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
	{
		Isolate(World, State);
		ZeroPlayer = Participant(World, State, 2, TEXT("Telemetry zero activity"));
		Leaver = Participant(World, State, 1, TEXT("Telemetry leaver"));
		FirstForce = ArmyTestSetup::SpawnGroup(World, PC, 0, ArmyTestSetup::FromFriendlyHQ(State, 700.f, 600.f, 100.f));
		SecondForce = ArmyTestSetup::SpawnGroup(World, PC, 1, ArmyTestSetup::FromFriendlyHQ(State, 1000.f, 600.f, 100.f));
		EnemyForce = ArmyTestSetup::SpawnGroup(World, nullptr, -1, ArmyTestSetup::HostileStaging(State));
		if (!Check(ZeroPlayer.IsValid() && Leaver.IsValid() && FirstForce.IsValid() && SecondForce.IsValid() && EnemyForce.IsValid(),
				TEXT("Map-derived isolated human and enemy fixtures exist")))
			return false;
		LeavingForce = ArmyTestSetup::SpawnGroup(World, Leaver.Get(), 0, ArmyTestSetup::FromFriendlyHQ(State, 1300.f, 650.f, 100.f));
		if (!Check(LeavingForce.IsValid(), TEXT("Leaver owns a real force fixture")))
			return false;
		for (AArmyGroup* Force : { FirstForce.Get(), SecondForce.Get(), EnemyForce.Get(), LeavingForce.Get() })
			Freeze(Force);
		return true;
	}
	bool FindPlacement(ACommandGameState* State, int32 Team, const FVector& Center, FVector& Location)
	{
		for (int32 Ring = 0; Ring < 9; ++Ring)
			for (int32 Direction = 0; Direction < 32; ++Direction)
			{
				const float Angle = Direction * PI / 16.f;
				FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
				Point.Z = 5.f;
				Point = State->ResolveBuildingLocation(ArmyTestSetup::WorkshopIndex, Point, Team);
				FString Reason;
				if (State->FindRegionAt(Point) == State->FindRegionAt(Center)
					&& State->ValidateBuildingPlacement(ArmyTestSetup::WorkshopIndex, Team, Point, Reason))
				{
					Location = Point;
					return true;
				}
			}
		return Check(false, TEXT("Map provides a valid workshop footprint"));
	}
	bool Commands(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
	{
		ACommandPlayerState* Host = PC->GetPlayerState<ACommandPlayerState>();
		AArmyGroup* const Multi[] = { FirstForce.Get(), SecondForce.Get() };
		AArmyGroup* const Duplicate[] = { FirstForce.Get(), FirstForce.Get() };
		const int32 Region = ArmyTestSetup::CurrentRegion(FirstForce.Get());
		if (!Check(FCommandService::IssueForceOrder(Host, Multi, EForceVerb::MoveHold, Region).IsAccepted(), TEXT("Multi-force verb command accepted once"))
			|| !Check(FCommandService::IssueForceOrder(Host, FirstForce.Get(), EForceVerb::Retreat).IsAccepted(), TEXT("Single-force overload accepted once"))
			|| !Check(!FCommandService::IssueForceOrder(Host, Duplicate, EForceVerb::MoveHold, Region).IsAccepted()
					&& !FCommandService::IssueForceOrder(Host, EnemyForce.Get(), EForceVerb::MoveHold, Region).IsAccepted()
					&& !FCommandService::IssueForceOrder(Host, FirstForce.Get(), EForceVerb::MoveHold, INDEX_NONE).IsAccepted()
					&& !FCommandService::IssueForceOrder(Host, FirstForce.Get(), static_cast<EForceVerb>(255), Region).IsAccepted(),
				TEXT("Duplicate selection, invalid ownership, region and verb orders reject"))
			|| !Check(FCommandService::SetRetreatThreshold(Host, Multi, ERetreatThreshold::Never).IsAccepted(),
				TEXT("Accepted threshold configuration is not a verb-order decision"))
			|| !Check(FCommandService::IssueForceOrder(State->EnemyCommander, EnemyForce.Get(), EForceVerb::MoveHold,
						  ArmyTestSetup::CurrentRegion(EnemyForce.Get()))
						  .IsAccepted(),
				TEXT("Accepted enemy order is excluded")))
			return false;
		Host->Resources = 4000;
		if (!FindPlacement(State, 0, State->FriendlyHeadquarters->GetActorLocation(), BuildLocation))
			return false;
		ACommandBuilding* Building = FCommandService::PlaceBuilding(Host, ArmyTestSetup::WorkshopIndex, BuildLocation).Building;
		if (!Check(Building != nullptr, TEXT("Real paid human building placement is accepted")))
			return false;
		Building->SetActorTickEnabled(false);
		if (!Check(!FCommandService::PlaceBuilding(Host, ArmyTestSetup::WorkshopIndex, BuildLocation).IsAccepted()
					&& !FCommandService::PlaceBuilding(Host, ArmyTestSetup::WorkshopIndex, ArmyTestSetup::OutsideArena(State)).IsAccepted(),
				TEXT("Overlapping and outside-arena building requests reject"))
			|| !Check(FCommandService::CancelBuilding(Host, Building).IsAccepted(), TEXT("Accepted cancellation is not another build decision")))
			return false;
		FVector EnemyLocation;
		State->EnemyCommander->Resources = 4000;
		if (!FindPlacement(State, 5, State->EnemyHeadquarters->GetActorLocation(), EnemyLocation))
			return false;
		ACommandBuilding* EnemyBuilding = FCommandService::PlaceBuilding(State->EnemyCommander, ArmyTestSetup::WorkshopIndex, EnemyLocation).Building;
		if (!Check(EnemyBuilding != nullptr, TEXT("Accepted enemy building placement is excluded")))
			return false;
		EnemyBuilding->SetActorTickEnabled(false);
		const FVector PingLocation = ArmyTestSetup::FromFriendlyHQ(State, 550.f, -500.f, 0.f);
		if (!Check(!FCommandService::Ping(PC, ArmyTestSetup::OutsideArena(State)).IsAccepted(), TEXT("Invalid ping rejected before throttle"))
			|| !Check(FCommandService::Ping(PC, PingLocation).IsAccepted(), TEXT("Real human ping accepted"))
			|| !Check(!FCommandService::Ping(PC, PingLocation).IsAccepted(), TEXT("Immediate ping rejected by real throttle")))
			return false;
		ACommandPlayerState* LeavingPlayer = Leaver->GetPlayerState<ACommandPlayerState>();
		if (!Check(FCommandService::IssueForceOrder(LeavingPlayer, LeavingForce.Get(), EForceVerb::MoveHold,
					   ArmyTestSetup::CurrentRegion(LeavingForce.Get()))
					   .IsAccepted(),
				TEXT("Leaver's real order accepted before disconnect")))
			return false;
		World->GetAuthGameMode<ACommandGameMode>()->Logout(Leaver.Get());
		Leaver->Destroy();
		if (IsValid(LeavingPlayer))
			LeavingPlayer->Destroy();
		return true;
	}
	bool ValidateFile(ACommandGameState* State, bool bFresh)
	{
		const FString& Path = State->MatchTelemetry->GetOutputPath();
		if (!Check(FPaths::GetPath(Path) == FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry"))
					&& FPaths::GetCleanFilename(Path) == TEXT("match-") + State->MatchTelemetry->GetMatchId() + TEXT(".json"),
				TEXT("Terminal output is a unique match JSON under Saved/Telemetry")))
			return false;
		FString Json;
		TSharedPtr<FJsonObject> Root;
		if (!Check(FFileHelper::LoadFileToString(Json, *Path) && FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root)
					&& Root.IsValid(),
				TEXT("Terminal telemetry is valid JSON")))
			return false;
		Test->AddInfo(FString::Printf(TEXT("Observed terminal telemetry JSON: %s"), *Path));
		double Seconds = 0.;
		FString Id, Result;
		const TArray<TSharedPtr<FJsonValue>>* Players = nullptr;
		const TSharedPtr<FJsonObject>* Ending = nullptr;
		if (!Check(Root->TryGetNumberField(TEXT("battle_seconds"), Seconds) && FMath::IsFinite(Seconds) && Seconds >= 3.
					&& Seconds == State->MatchTelemetry->GetBattleSeconds()
					&& Root->TryGetStringField(TEXT("match_id"), Id) && Id == State->MatchTelemetry->GetMatchId()
					&& Root->TryGetStringField(TEXT("result"), Result) && Result == TEXT("Victory")
					&& Root->TryGetArrayField(TEXT("players"), Players) && Players->Num() == (bFresh ? 1 : 3)
					&& Root->TryGetObjectField(TEXT("ending"), Ending),
				TEXT("Exact result, full battle duration and complete human roster fields exist")))
			return false;
		TSet<FString> Ids;
		bool Seen[3] = {};
		for (const TSharedPtr<FJsonValue>& Value : *Players)
		{
			if (!Check(Value.IsValid() && Value->Type == EJson::Object, TEXT("Every player entry is an object")))
				return false;
			const TSharedPtr<FJsonObject> Player = Value->AsObject();
			double Commander = -1.;
			FString PlayerId, Name;
			bool bDisconnected = false;
			if (!Check(Player->TryGetNumberField(TEXT("commander_index"), Commander)
						&& Player->TryGetStringField(TEXT("player_id"), PlayerId) && !PlayerId.IsEmpty() && !Ids.Contains(PlayerId)
						&& Player->TryGetStringField(TEXT("player_name"), Name)
						&& Player->TryGetBoolField(TEXT("disconnected"), bDisconnected),
					TEXT("Player identity, commander, name and connection snapshot are present and distinct")))
				return false;
			Ids.Add(PlayerId);
			const int32 Index = static_cast<int32>(Commander);
			const bool bHost = Index == HostIndex;
			if (!Check(Index >= 0 && Index < 3 && !Seen[Index] && (bHost || !bFresh), TEXT("No enemy, removed slot or duplicate player appears")))
				return false;
			Seen[Index] = true;
			const int32 Orders = bFresh ? 0 : bHost ? 2
				: Index == 1                        ? 1
													: 0;
			const int32 Builds = !bFresh && bHost ? 1 : 0;
			const int32 Pings = !bFresh && bHost ? 1 : 0;
			if (!Check(Name == (bHost ? HostName : Index == 1 ? TEXT("Telemetry leaver")
															  : TEXT("Telemetry zero activity"))
						&& bDisconnected == (!bFresh && Index == 1),
					TEXT("Zero-action human and destroyed leaver retain exact attribution")))
				return false;
			const TCHAR* Counts[] = { TEXT("orders"), TEXT("builds"), TEXT("pings") };
			const TCHAR* Rates[] = { TEXT("orders_per_minute"), TEXT("builds_per_minute"), TEXT("pings_per_minute"), TEXT("decisions_per_minute") };
			const int32 Expected[] = { Orders, Builds, Pings, Orders + Builds + Pings };
			for (int32 Category = 0; Category < 4; ++Category)
			{
				double Count = 0., Rate = 0.;
				if (!Check((Category == 3 || (Player->TryGetNumberField(Counts[Category], Count) && Count == Expected[Category]))
							&& Player->TryGetNumberField(Rates[Category], Rate) && FMath::IsFinite(Rate)
							&& FMath::IsNearlyEqual(Rate, Expected[Category] * 60. / Seconds, 1.e-9),
						TEXT("Exact accepted counts and finite rates use the full battle-minute denominator")))
					return false;
			}
		}
		const AMapRegion* Region = State->FindRegionAt(State->EnemyHeadquarters->GetActorLocation());
		FString Cause, RegionName;
		double RegionIndex = INDEX_NONE;
		const TSharedPtr<FJsonObject>* Location = nullptr;
		if (!Check(Region && (*Ending)->TryGetStringField(TEXT("cause"), Cause) && Cause == TEXT("enemy_headquarters_destroyed")
					&& (*Ending)->TryGetNumberField(TEXT("region_index"), RegionIndex) && RegionIndex == Region->RegionIndex
					&& (*Ending)->TryGetStringField(TEXT("region_name"), RegionName) && RegionName == Region->DisplayName.ToString()
					&& (*Ending)->TryGetObjectField(TEXT("location"), Location),
				TEXT("Terminal region, name and cause derive from the destroyed map HQ")))
			return false;
		const FVector Spot = State->EnemyHeadquarters->GetActorLocation();
		double X = 0., Y = 0., Z = 0.;
		return Check((*Location)->TryGetNumberField(TEXT("x"), X) && X == Spot.X
				&& (*Location)->TryGetNumberField(TEXT("y"), Y) && Y == Spot.Y
				&& (*Location)->TryGetNumberField(TEXT("z"), Z) && Z == Spot.Z,
			TEXT("Ending location is the exact map-derived HQ position"));
	}

	FAutomationTestBase* Test;
	double Started;
	double StageStarted = 0.;
	double PausedSeconds = 0.;
	double OriginalSeconds = 0.;
	int32 Stage = 0;
	int32 HostIndex = INDEX_NONE;
	FString HostName, OriginalId, OriginalPath, OriginalJson;
	FDateTime OriginalTimestamp;
	FVector BuildLocation = FVector::ZeroVector;
	TWeakObjectPtr<ACommandGameState> OriginalState;
	TWeakObjectPtr<UMatchTelemetry> OriginalTelemetry;
	TWeakObjectPtr<ACommandPlayerController> ZeroPlayer, Leaver;
	TWeakObjectPtr<AArmyGroup> FirstForce, SecondForce, EnemyForce, LeavingForce;
};
}

bool FMatchTelemetryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(MatchTelemetryScenarioTests::FMatchScenario(this));
	return true;
}

#endif
