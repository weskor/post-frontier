#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "MatchTelemetryScenario.h"
#include "ArmyTestSetup.h"
#include "GuardedHqTestSupport.h"
#include "MatchTelemetry.h"
#include "ArmyUnit.h"
#include "CommandGameMode.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "OnlineSubsystemTypes.h"

namespace MatchTelemetryScenarioTests
{
bool FMatchScenario::Check(bool Value, const TCHAR* Message)
{
	if (!Value)
	{
		Test->AddError(Message);
		Cleanup();
	}
	return Value;
}

bool FMatchScenario::Fail(const TCHAR* Message)
{
	Test->AddError(Message);
	Cleanup();
	return true;
}

FString FMatchScenario::PathFor(const UMatchTelemetry* Telemetry) const
{
	return FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("Telemetry"),
		TEXT("match-") + Telemetry->GetMatchId() + TEXT(".json"));
}

void FMatchScenario::Track(ACommandGameState* State)
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

void FMatchScenario::Cleanup()
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

void FMatchScenario::Leave(UWorld* World, ACommandPlayerController* PC)
{
	if (!IsValid(PC))
		return;
	ACommandPlayerState* Player = PC->GetPlayerState<ACommandPlayerState>();
	World->GetAuthGameMode<ACommandGameMode>()->Logout(PC);
	PC->Destroy();
	if (IsValid(Player))
		Player->Destroy();
}

bool FMatchScenario::EndWithHQ(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
{
	AArmyGroup* Attacker = ArmyTestSetup::SpawnGroup(World, PC, 0,
		ArmyTestSetup::FromFriendlyHQ(State, 700.f, 600.f, 100.f));
	if (!Check(Attacker && !Attacker->GetUnits().IsEmpty(), TEXT("Fresh match has an explicit unpaid terminal attacker")))
		return false;
	Freeze(Attacker);
	GuardedHqTest::TakeOffline(*State->EnemyHeadquarters, *Attacker->GetUnits()[0]);
	// An HQ at 0 HP is offline; the battle ends when its hold completes.
	return Check(GuardedHqTest::CompleteHold(*State->EnemyHeadquarters, Attacker->GetUnits()[0]),
		TEXT("The attackers complete the hold on the offline HQ"));
}

bool FMatchScenario::OriginalUnchanged()
{
	FString Json;
	return Check(FFileHelper::LoadFileToString(Json, *OriginalPath) && Json == OriginalJson
			&& IFileManager::Get().GetTimeStamp(*OriginalPath) == OriginalTimestamp,
		TEXT("Real terminal EndPlay/restarts/travel preserve the original file without duplicate writes"));
}

void FMatchScenario::Isolate(UWorld* World, ACommandGameState* State)
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

void FMatchScenario::Freeze(AArmyGroup* Force)
{
	Force->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Force->GetUnits())
		Unit->SetActorTickEnabled(false);
}

ACommandPlayerController* FMatchScenario::Participant(UWorld* World, ACommandGameState* State, int32 Index, const TCHAR* Name,
	const TCHAR* OnlineId)
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
	{
		const FUniqueNetIdRef Identity = FUniqueNetIdString::Create(FString(OnlineId), FName(TEXT("NULL")));
		Player->SetUniqueId(FUniqueNetIdRepl(Identity));
	}
	State->AddPlayerState(Player);
	return PC;
}

bool FMatchScenario::Setup(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
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

bool FMatchScenario::FindPlacement(ACommandGameState* State, int32 Team, const FVector& Center, FVector& Location)
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

bool FMatchScenario::Commands(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
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
}

#endif
