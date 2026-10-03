#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CommandGameMode.h"
#include "MatchTelemetry.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "HAL/PlatformTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OnlineSubsystemTypes.h"
#include "Serialization/JsonSerializer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMatchTelemetryTest, "CoopRTS.Telemetry.Match",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace MatchTelemetryScenarioTests
{
// Fresh standalone matches; only explicit fixtures act. Exercise real accepted
// commands, logout/reconnect identity, HQ outcomes, restart and ongoing travel.
class FMatchScenario : public IAutomationLatentCommand
{
public:
	explicit FMatchScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	~FMatchScenario() override { Cleanup(); }

	bool Update() override
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
		if (Stage == 1)
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
		if (Stage == 2)
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
		if (Stage == 3)
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
		if (Stage == 4)
		{
			if (ArmyTestSetup::GameSeconds(World) - StageStarted < 1.1)
				return false;
			LastLeft = State->MatchTelemetry->GetBattleSeconds();
			Leave(World, Leaver.Get());
			State->EnemyHeadquarters->ReceiveAttack(State->EnemyHeadquarters->Health, FirstForce->GetUnits()[0]);
			Stage = 5;
			return false;
		}
		if (Stage == 5)
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
		if (Stage == 6)
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
		if (Stage == 7)
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
		if (Stage == 8)
		{
			if (State->MatchResult == EMatchResult::Ongoing)
				return false;
			if (!ValidateFile(State, true) || !OriginalUnchanged()
				|| !Check(FCommandService::Restart(PC).IsAccepted(), TEXT("Terminal restart reaches the abandonment fixture")))
				return true;
			Stage = 9;
			return false;
		}
		if (Stage == 9)
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
		if (Stage == 10)
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
		if (Stage == 11)
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
		if (State->MatchResult == EMatchResult::Ongoing)
			return false;
		if (!ValidateFile(State, true) || !OriginalUnchanged())
			return true;
		Test->AddInfo(TEXT("Telemetry proof: UUID-only attribution with no online IDs/hostnames/names; exact accepted counts and connected-participation rates; late join, real logout, same opaque-ID reconnect and gaps, zero-action and zero-duration humans retained; pause exclusion, real HQ cause/location, terminal and travel abandonment idempotence, fresh seamless restarts. JSON evidence logged before test-only cleanup."));
		Cleanup();
		return true;
	}

private:
	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
		{
			Test->AddError(Message);
			Cleanup();
		}
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		Cleanup();
		return true;
	}
	FString PathFor(const UMatchTelemetry* Telemetry) const
	{
		return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry"),
			TEXT("match-") + Telemetry->GetMatchId() + TEXT(".json"));
	}
	void Track(ACommandGameState* State)
	{
		if (!State || !State->MatchTelemetry || State->MatchTelemetry->GetMatchId().IsEmpty()
			|| TrackedTelemetry.Contains(State->MatchTelemetry.Get()))
			return;
		const FString Path = PathFor(State->MatchTelemetry);
		// Claim only an unflushed component observed by this scenario, never an
		// earlier test's existing record or an unrelated Saved/Telemetry file.
		if (!State->MatchTelemetry->GetOutputPath().IsEmpty() || IFileManager::Get().FileExists(*Path))
			return;
		CreatedPaths.Add(Path);
		TrackedTelemetry.Add(State->MatchTelemetry.Get());
	}
	void Cleanup()
	{
		if (bCleaned)
			return;
		bCleaned = true;
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		Track(State);
		if (World && World->IsPaused())
			FCommandService::Resume(ArmyTestSetup::Controller(World));
		// Close any still-live fixture before deletion: shutdown must not create a
		// new abandoned file after the latent test has relinquished ownership.
		for (const TWeakObjectPtr<UMatchTelemetry>& Weak : TrackedTelemetry)
			if (UMatchTelemetry* Telemetry = Weak.Get())
				if (ACommandGameState* Owner = Cast<ACommandGameState>(Telemetry->GetOwner()))
					if (Owner->MatchResult == EMatchResult::Ongoing && Telemetry->GetOutputPath().IsEmpty())
						Owner->SetMatchResult(EMatchResult::Defeat);
		for (const FString& Path : CreatedPaths)
		{
			if (!IFileManager::Get().FileExists(*Path))
				continue;
			FString Json;
			if (FFileHelper::LoadFileToString(Json, *Path))
				Test->AddInfo(FString::Printf(TEXT("Telemetry evidence before cleanup (%s):\n%s"), *Path, *Json));
			if (!IFileManager::Get().Delete(*Path, true))
				Test->AddError(FString::Printf(TEXT("Unable to delete test-created telemetry JSON: %s"), *Path));
		}
	}
	void Leave(UWorld* World, ACommandPlayerController* PC)
	{
		if (!IsValid(PC))
			return;
		ACommandPlayerState* Player = PC->GetPlayerState<ACommandPlayerState>();
		World->GetAuthGameMode<ACommandGameMode>()->Logout(PC);
		PC->Destroy();
		if (IsValid(Player))
			Player->Destroy();
	}
	bool EndWithHQ(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
	{
		AArmyGroup* Attacker = ArmyTestSetup::SpawnGroup(World, PC, 0,
			ArmyTestSetup::FromFriendlyHQ(State, 700.f, 600.f, 100.f));
		if (!Check(Attacker && !Attacker->GetUnits().IsEmpty(), TEXT("Fresh match has an explicit unpaid terminal attacker")))
			return false;
		Freeze(Attacker);
		State->EnemyHeadquarters->ReceiveAttack(State->EnemyHeadquarters->Health, Attacker->GetUnits()[0]);
		return true;
	}
	bool OriginalUnchanged()
	{
		FString Json;
		return Check(FFileHelper::LoadFileToString(Json, *OriginalPath) && Json == OriginalJson
				&& IFileManager::Get().GetTimeStamp(*OriginalPath) == OriginalTimestamp,
			TEXT("Real terminal EndPlay/restarts/travel preserve the original file without duplicate writes"));
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
	ACommandPlayerController* Participant(UWorld* World, ACommandGameState* State, int32 Index, const TCHAR* Name,
		const TCHAR* OnlineId = nullptr)
	{
		ACommandPlayerController* PC = World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* Player = World->SpawnActor<ACommandPlayerState>();
		if (!Check(PC && Player, TEXT("Human roster fixture spawns")))
			return nullptr;
		PC->SetPlayerState(Player);
		Player->SetOwner(PC);
		Player->CommanderIndex = Index;
		Player->SetPlayerName(Name);
		if (OnlineId)
			Player->SetUniqueId(FUniqueNetIdRepl(FUniqueNetIdString::Create(FString(OnlineId), FName(TEXT("NULL")))));
		State->AddPlayerState(Player);
		return PC;
	}
	bool Setup(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
	{
		Isolate(World, State);
		ZeroPlayer = Participant(World, State, 2, TEXT("Telemetry zero activity"));
		ZeroJoined = State->MatchTelemetry->GetBattleSeconds();
		Leaver = Participant(World, State, 1, TEXT("Telemetry leaver"), PrivateOnlineId);
		FirstJoined = State->MatchTelemetry->GetBattleSeconds();
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
		return true;
	}
	bool IsNull(const TSharedPtr<FJsonObject>& Object, const TCHAR* Key) const
	{
		const TSharedPtr<FJsonValue>* Value = Object->Values.Find(Key);
		return Value && Value->IsValid() && (*Value)->Type == EJson::Null;
	}
	bool ReadEvidence(const FString& Path, TSharedPtr<FJsonObject>& Root)
	{
		FString Json;
		if (!Check(FFileHelper::LoadFileToString(Json, *Path), TEXT("Telemetry output exists and loads")))
			return false;
		Test->AddInfo(FString::Printf(TEXT("Observed telemetry JSON (%s):\n%s"), *Path, *Json));
		if (!Check(FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Root) && Root.IsValid(),
				TEXT("Telemetry output is valid JSON")))
			return false;
		const TCHAR* PrivateStrings[] = { PrivateOnlineId, TEXT("Telemetry zero activity"), TEXT("Telemetry leaver"),
			TEXT("Telemetry reconnected private name"), TEXT("Telemetry instant private name"), FPlatformProcess::ComputerName() };
		for (const TCHAR* Private : PrivateStrings)
			if (!Check(!Json.Contains(Private), TEXT("Serialized consumer output excludes account IDs, hostnames and player names")))
				return false;
		return Check(HostName.IsEmpty() || !Json.Contains(HostName), TEXT("Host player name is absent from serialized output"));
	}
	bool ValidatePlayers(const TSharedPtr<FJsonObject>& Root, double Seconds, bool bFresh)
	{
		const TArray<TSharedPtr<FJsonValue>>* Players = nullptr;
		if (!Check(Root->TryGetArrayField(TEXT("players"), Players) && Players->Num() == (bFresh ? 1 : 4),
				TEXT("Complete human roster retains zero-action/zero-duration humans and merges the real reconnect")))
			return false;
		TSet<FString> Ids;
		bool Seen[4] = {};
		for (const TSharedPtr<FJsonValue>& Value : *Players)
		{
			if (!Check(Value.IsValid() && Value->Type == EJson::Object, TEXT("Every player entry is an object")))
				return false;
			const TSharedPtr<FJsonObject> Player = Value->AsObject();
			double Commander = -1., Joined = -1., Participation = -1.;
			FString PlayerId;
			FGuid Pseudonym;
			bool bDisconnected = false;
			if (!Check(Player->Values.Num() == 13 && !Player->HasField(TEXT("player_name"))
						&& Player->TryGetNumberField(TEXT("commander_index"), Commander)
						&& Player->TryGetStringField(TEXT("player_id"), PlayerId)
						&& FGuid::ParseExact(PlayerId, EGuidFormats::DigitsWithHyphens, Pseudonym) && Pseudonym.IsValid()
						&& !Ids.Contains(PlayerId) && !OriginalPlayerIds.Contains(PlayerId)
						&& Player->TryGetBoolField(TEXT("disconnected"), bDisconnected)
						&& Player->TryGetNumberField(TEXT("joined_seconds"), Joined) && FMath::IsFinite(Joined) && Joined >= 0.
						&& Player->TryGetNumberField(TEXT("participation_seconds"), Participation) && FMath::IsFinite(Participation),
					TEXT("Only commander slot and distinct random match-local UUID attribute players; participation fields are finite")))
				return false;
			Ids.Add(PlayerId);
			const int32 Index = static_cast<int32>(Commander);
			const bool bHost = Index == HostIndex;
			if (!Check(Commander == Index && Index >= 0 && Index < 4 && !Seen[Index] && (bHost || !bFresh),
					TEXT("No enemy, duplicate slot or extra reconnect participant appears")))
				return false;
			Seen[Index] = true;
			const bool bLeft = !bFresh && (Index == 1 || Index == 3);
			const double ExpectedJoined = bFresh || bHost ? Joined
				: Index == 1                              ? FirstJoined
				: Index == 2                              ? ZeroJoined
														  : InstantJoined;
			const double ExpectedParticipation = !bFresh && Index == 1
				? FirstLeft - FirstJoined + LastLeft - Rejoined
				: !bFresh && Index == 3 ? 0.
										: Seconds - ExpectedJoined;
			double Left = -1.;
			if (!Check(Joined == ExpectedJoined && Joined <= Seconds && (bFresh || !bHost || Joined <= FirstJoined)
						&& bDisconnected == bLeft
						&& (bLeft ? Player->TryGetNumberField(TEXT("left_seconds"), Left) && Left == (Index == 1 ? LastLeft : InstantJoined)
								  : IsNull(Player, TEXT("left_seconds")))
						&& FMath::IsNearlyEqual(Participation, ExpectedParticipation, 1.e-9) && Participation >= 0.
						&& (bFresh || bHost || Index == 3 || (Joined >= 3. && Participation < Seconds))
						&& (bFresh || Index != 1 || (FirstLeft > FirstJoined && Rejoined > FirstLeft && LastLeft > Rejoined)),
					TEXT("Exact first join, last logout and connected intervals exclude prejoin, pause and reconnect gaps")))
				return false;
			const int32 Orders = bFresh ? 0 : bHost || Index == 1 ? 2
				: Index == 3                                      ? 1
																  : 0;
			const int32 Builds = !bFresh && bHost ? 1 : 0;
			const int32 Pings = !bFresh && bHost ? 1 : 0;
			const TCHAR* Counts[] = { TEXT("orders"), TEXT("builds"), TEXT("pings") };
			const TCHAR* Rates[] = { TEXT("orders_per_minute"), TEXT("builds_per_minute"), TEXT("pings_per_minute"), TEXT("decisions_per_minute") };
			const int32 Expected[] = { Orders, Builds, Pings, Orders + Builds + Pings };
			for (int32 Category = 0; Category < 4; ++Category)
			{
				double Count = 0., Rate = 0.;
				const double ExpectedRate = ExpectedParticipation > 0. ? Expected[Category] * 60. / ExpectedParticipation : 0.;
				if (!Check((Category == 3 || (Player->TryGetNumberField(Counts[Category], Count) && Count == Expected[Category]))
							&& Player->TryGetNumberField(Rates[Category], Rate) && FMath::IsFinite(Rate)
							&& FMath::IsNearlyEqual(Rate, ExpectedRate, 1.e-9),
						TEXT("Consumer JSON has exact accepted counts and participation-minute rates, including finite zero-duration rates")))
					return false;
			}
		}
		for (const FString& Id : Ids)
			OriginalPlayerIds.Add(Id);
		return true;
	}
	bool ValidateAbandoned()
	{
		TSharedPtr<FJsonObject> Root;
		if (!ReadEvidence(AbandonedPath, Root))
			return false;
		double Schema = 0., Seconds = -1., RegionIndex = 0.;
		FString Id, Result, Map, Cause, RegionName;
		const TSharedPtr<FJsonObject>* Ending = nullptr;
		if (!Check(Root->TryGetNumberField(TEXT("schema_version"), Schema) && Schema == 2
					&& Root->TryGetNumberField(TEXT("battle_seconds"), Seconds) && FMath::IsFinite(Seconds) && Seconds >= 3.
					&& Root->TryGetStringField(TEXT("match_id"), Id) && Id == AbandonedId
					&& Root->TryGetStringField(TEXT("map"), Map) && Map == AbandonedMap
					&& Root->TryGetStringField(TEXT("result"), Result) && Result == TEXT("Abandoned")
					&& Root->TryGetObjectField(TEXT("ending"), Ending)
					&& (*Ending)->TryGetStringField(TEXT("cause"), Cause) && Cause == TEXT("level_transition")
					&& (*Ending)->TryGetNumberField(TEXT("region_index"), RegionIndex) && RegionIndex == INDEX_NONE
					&& (*Ending)->TryGetStringField(TEXT("region_name"), RegionName) && RegionName.IsEmpty()
					&& IsNull(*Ending, TEXT("location")),
				TEXT("Real ongoing travel writes Abandoned with truthful transition cause and explicitly unknown ending location")))
			return false;
		return ValidatePlayers(Root, Seconds, true);
	}
	bool ValidateFile(ACommandGameState* State, bool bFresh)
	{
		const FString& Path = State->MatchTelemetry->GetOutputPath();
		if (!Check(Path == PathFor(State->MatchTelemetry), TEXT("Terminal output is a unique match JSON under Saved/Telemetry")))
			return false;
		TSharedPtr<FJsonObject> Root;
		if (!ReadEvidence(Path, Root))
			return false;
		double Schema = 0., Seconds = 0.;
		FString Id, Result;
		const TSharedPtr<FJsonObject>* Ending = nullptr;
		if (!Check(Root->TryGetNumberField(TEXT("schema_version"), Schema) && Schema == 2
					&& Root->TryGetNumberField(TEXT("battle_seconds"), Seconds) && FMath::IsFinite(Seconds) && Seconds >= 3.
					&& Seconds == State->MatchTelemetry->GetBattleSeconds()
					&& Root->TryGetStringField(TEXT("match_id"), Id) && Id == State->MatchTelemetry->GetMatchId()
					&& Root->TryGetStringField(TEXT("result"), Result) && Result == TEXT("Victory")
					&& Root->TryGetObjectField(TEXT("ending"), Ending),
				TEXT("Exact version, real terminal result and full simulation battle duration fields exist")))
			return false;
		if (!ValidatePlayers(Root, Seconds, bFresh))
			return false;
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
	double ZeroJoined = 0., FirstJoined = 0., FirstLeft = 0., Rejoined = 0., LastLeft = 0., InstantJoined = 0.;
	bool bCleaned = false;
	const TCHAR* PrivateOnlineId = TEXT("telemetry-private-hostname-76561198012345678");
	TSet<FString> CreatedPaths;
	TArray<TWeakObjectPtr<UMatchTelemetry>> TrackedTelemetry;
	TSet<FString> OriginalPlayerIds;
	FString AbandonedId, AbandonedPath, AbandonedMap, AbandonedJson;
	FDateTime AbandonedTimestamp;
	int32 Stage = 0;
	int32 HostIndex = INDEX_NONE;
	FString HostName, OriginalId, OriginalPath, OriginalJson;
	FDateTime OriginalTimestamp;
	FVector BuildLocation = FVector::ZeroVector;
	TWeakObjectPtr<ACommandGameState> OriginalState, RestartState, AbandonedState;
	TWeakObjectPtr<UMatchTelemetry> OriginalTelemetry, AbandonedTelemetry;
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
