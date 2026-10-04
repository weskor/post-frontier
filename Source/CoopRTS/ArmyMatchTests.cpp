#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "DepositSite.h"
#include "GuardedHqTestSupport.h"
#include "Headquarters.h"
#include "SimulationSettings.h"
#include "Content/BuildingDefinition.h"
#include "GameState/GameStatePlanning.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyMatchVictoryTest, "CoopRTS.Match.VictoryRestart",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyMatchDefeatTest, "CoopRTS.Match.DefeatRestart",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

class FArmyMatchScenario : public IAutomationLatentCommand
{
public:
	FArmyMatchScenario(FAutomationTestBase* InTest, bool bInVictory) : Test(InTest), bVictory(bInVictory) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - StartedAt > 60.)
			return Fail(*FString::Printf(TEXT("Match scenario timed out in stage %d (victory=%d)"), Stage, bVictory));
		UWorld* World = ArmyTestSetup::World();
		if (!World)
			return false;
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!State || !PC || !Wallet || Wallet->CommanderIndex < 0 || !ArmyTestSetup::MapReady(State))
			return false;
		if (Stage == 0)
			return RunStage0(World, State, PC, Wallet);
		if (Stage == 1)
			return RunStage1(State, PC, Wallet);
		return RunStage2(World, State, Wallet);
	}
private:
	bool RunStage0(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC, ACommandPlayerState* Wallet)
	{
		State->bVerificationIncomePaused = true;
		// Paid production may already have created an empty force before automation starts.
		// Arrange our own opponents rather than mistaking that force for a combat fixture.
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->TeamIndex == 5)
				Building->bProductionEnabled = false;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		Friendly = ArmyTestSetup::SpawnGroup(World, PC, 0, ArmyTestSetup::FromFriendlyHQ(State, 1700.f, 600.f, 100.f));
		Enemy = ArmyTestSetup::SpawnGroup(World, nullptr, -1, ArmyTestSetup::HostileStaging(State));
		if (!Friendly.IsValid() || !Enemy.IsValid())
			return Fail(TEXT("Live outcome fixture armies missing"));
		const int32 BeforeResearch = Wallet->Resources;
		ArmyTestSetup::Research(PC, EArmyDoctrine::SiegeOptics);
		if (Wallet->Doctrine != EArmyDoctrine::SiegeOptics
			|| Wallet->Resources != BeforeResearch - ACommandBuilding::ResearchCost)
			return Fail(TEXT("Old match must own a paid specialization"));
		const UBuildingDefinition* Barracks = State->Content->Building(ArmyTestSetup::BarracksIndex);
		if (!Barracks || Wallet->Resources < ACommandBuilding::GetBuildCost(*Barracks))
			return Fail(TEXT("Terminal construction fixture must afford a barracks"));
		MoveLocation = Friendly->GetCenter();
		if (!State->Arena->ContainsTravel(MoveLocation))
			return Fail(TEXT("Terminal Move fixture must be inside arena travel bounds"));
		if (!FindBuildingLocation(State))
			return Fail(TEXT("Terminal placement requires a previously valid HQ-region footprint"));
		for (ACapturePoint* Site : State->CaptureSites)
		{
			Site->ControllingTeam = 0;
			Site->CaptureProgress = 1.f;
		}
		for (ADepositSite* Deposit : State->Deposits)
			Deposit->Remaining = 1;
		return BeginEncounter(World, State, Wallet);
	}
	bool BeginEncounter(UWorld* World, ACommandGameState* State, ACommandPlayerState* Wallet)
	{
		AHeadquarters* Target = bVictory ? State->EnemyHeadquarters : State->FriendlyHeadquarters;
		AArmyGroup* Attacker = bVictory ? Friendly.Get() : Enemy.Get();
		Target->Health = 80; // Short encounter fixture; real weapons deliver every subsequent hit.
		FVector Approach = (bVictory ? State->FriendlyHeadquarters : State->EnemyHeadquarters)->GetActorLocation()
			- Target->GetActorLocation();
		Approach.Z = 0.f;
		Approach.Normalize();
		for (AArmyUnit* Unit : Attacker->GetUnits())
		{
			Unit->SetActorLocation(Target->GetActorLocation() + Approach * 650.f
					+ FVector(-Approach.Y, Approach.X, 0.f) * ((Unit->GetCompositionSlot() - 2.5f) * 100.f),
				false, nullptr, ETeleportType::TeleportPhysics);
			BeforeShots += Unit->AttackCount;
		}
		if (bVictory)
			FCommandService::IssueForceOrder(Wallet, Attacker, EForceVerb::Attack, INDEX_NONE, Target);
		else
			FCommandService::IssueForceOrder(State->EnemyCommander, Attacker, EForceVerb::Attack, INDEX_NONE, Target);
		if (Attacker->Verb != EForceVerb::Attack || Attacker->TargetStructure != Target)
			return Fail(TEXT("A targeted HQ attack must be accepted without a shield prerequisite"));
		OldState = State;
		OldWorld = World;
		Stage = 1;
		StartedAt = FPlatformTime::Seconds();
		Test->AddInfo(TEXT("Real HQ attack in progress; waiting for weapon-caused outcome."));
		return false;
	}
	bool RunStage1(ACommandGameState* State, ACommandPlayerController* PC, ACommandPlayerState* Wallet)
	{
		AHeadquarters* Target = bVictory ? State->EnemyHeadquarters : State->FriendlyHeadquarters;
		if (State->MatchResult == EMatchResult::Ongoing)
		{
			// An HQ at 0 HP is only offline: the battle goes on until the attackers complete the hold on its main.
			if (!bHoldCompleted && Target->IsOffline())
			{
				if (!Target->IsAlive() || Target->Health != 0)
					return Fail(TEXT("A weapon-killed HQ must be offline at 0 HP, not lost"));
				const AArmyGroup* Attacker = bVictory ? Friendly.Get() : Enemy.Get();
				bHoldCompleted = true;
				if (!GuardedHqTest::CompleteHold(*Target, Attacker->GetUnits()[0]))
					return Fail(TEXT("Attackers in the main must complete the hold"));
			}
			return false;
		}
		const EMatchResult Expected = bVictory ? EMatchResult::Victory : EMatchResult::Defeat;
		if (State->MatchResult != Expected || !Friendly.IsValid() || !Enemy.IsValid())
			return Fail(TEXT("Weapon encounter resolved to the wrong outcome"));
		const AArmyGroup* Attacker = bVictory ? Friendly.Get() : Enemy.Get();
		uint32 AfterShots = 0;
		for (const AArmyUnit* Unit : Attacker->GetUnits())
			AfterShots += Unit->AttackCount;
		if (AfterShots <= BeforeShots || (bVictory ? State->EnemyHeadquarters->Health : State->FriendlyHeadquarters->Health) != 0)
			return Fail(TEXT("Outcome requires real attacks and zero target HQ health"));
		if (!bVictory)
		{
			// The lethal hit is already proved. Isolate terminal rejection from the
			// dead home HQ and hostile occupation, which independently deny building.
			State->FriendlyHeadquarters->ResetForTest(80);
			for (AArmyUnit* Unit : Enemy->GetUnits())
				Unit->SetActorLocation(ArmyTestSetup::HostileStaging(State)
						+ FVector(0.f, Unit->GetCompositionSlot() * 100.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
		}
		if (!State->IsInBuildTerritory(ArmyTestSetup::BarracksIndex, 0, BuildingLocation))
			return Fail(TEXT("Terminal construction fixture must remain in uncontested friendly territory"));
		const uint32 Serial = Friendly->OrderSerial;
		const int32 Balance = Wallet->Resources;
		const int32 Buildings = State->Buildings.Num();
		FCommandService::IssueForceOrder(Wallet, Friendly.Get(), EForceVerb::MoveHold, ArmyTestSetup::RegionAt(State, MoveLocation));
		FCommandService::PlaceBuilding(Wallet, ArmyTestSetup::BarracksIndex, BuildingLocation);
		ArmyTestSetup::Research(PC, EArmyDoctrine::FieldRepairs);
		State->bVerificationIncomePaused = false; // Terminal state, not the fixture pause, must stop income.
		State->Tick(2.f);
		if (Friendly->OrderSerial != Serial || Wallet->Resources != Balance || State->Buildings.Num() != Buildings
			|| Wallet->Doctrine != EArmyDoctrine::SiegeOptics)
			return Fail(TEXT("Terminal match must reject orders/construction/research and stop income"));
		Slot = Wallet->CommanderIndex;
		FCommandService::Restart(PC);
		Stage = 2;
		StartedAt = FPlatformTime::Seconds();
		Test->AddInfo(TEXT("Outcome and terminal guards observed; seamless fresh world requested."));
		return false;
	}
	bool RunStage2(UWorld* World, ACommandGameState* State, ACommandPlayerState* Wallet)
	{
		if (World == OldWorld.Get() || State == OldState.Get() || Wallet->CommanderIndex < 0)
			return false;
		if (State->MatchResult != EMatchResult::Ongoing || Wallet->CommanderIndex != Slot
			|| Wallet->Doctrine != EArmyDoctrine::None
			|| Wallet->Resources < GameStatePlanning::FixtureStartingResources
			|| Wallet->Resources > GameStatePlanning::FixtureStartingResources + 2 * State->GetIncomePerSecond(Wallet)
			|| State->FriendlyHeadquarters->Health != State->FriendlyHeadquarters->MaxHealth()
			|| State->EnemyHeadquarters->Health != State->EnemyHeadquarters->MaxHealth()
			|| State->GetIncomePerSecond(Wallet) != State->GetHumanBaselineIncomePerSecond())
			return Fail(TEXT("Restart must preserve commander identity but reset economy/research/HQs/territory"));
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (It->GetTeamIndex() == 0)
				return Fail(TEXT("Fresh world must not recreate fixed player armies"));
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->TeamIndex == 0)
				return Fail(TEXT("Old player buildings must not survive restart"));
		for (ACapturePoint* Site : State->CaptureSites)
			if (!IsValid(Site) || Site->ControllingTeam != -1 || Site->CaptureProgress != 0.f)
				return Fail(TEXT("New sectors must start neutral"));
		for (const ADepositSite* Deposit : State->Deposits)
			if (!IsValid(Deposit) || IsValid(Deposit->Extractor)
				|| Deposit->Remaining != (Deposit->bRich ? FSimulationSettings::ForWorld(World).RichAmount : FSimulationSettings::ForWorld(World).NormalAmount))
				return Fail(TEXT("Fresh deposits must reset occupancy and finite reserves"));
		Test->AddInfo(TEXT("Real weapon outcome, terminal command/economy guards and fresh construction match restart passed."));
		return true;
	}
	bool FindBuildingLocation(ACommandGameState* State)
	{
		const FVector Center = State->FriendlyHeadquarters->GetActorLocation();
		for (int32 Ring = 0; Ring < 9; ++Ring)
			for (int32 Direction = 0; Direction < 32; ++Direction)
			{
				const float Angle = Direction * PI / 16.f;
				const FVector Candidate = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
				const FVector Point = State->ResolveBuildingLocation(ArmyTestSetup::BarracksIndex, Candidate);
				if (State->FindRegionAt(Point) != State->FindRegionAt(Center))
					continue;
				FString Reason;
				if (State->ValidateBuildingPlacement(ArmyTestSetup::BarracksIndex, 0, Point, Reason))
				{
					BuildingLocation = Point;
					return true;
				}
			}
		return false;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	FAutomationTestBase* Test;
	bool bVictory;
	int32 Stage = 0;
	bool bHoldCompleted = false;
	int32 Slot = -1;
	uint32 BeforeShots = 0;
	double StartedAt = FPlatformTime::Seconds();
	FVector BuildingLocation = FVector::ZeroVector;
	FVector MoveLocation = FVector::ZeroVector;
	TWeakObjectPtr<UWorld> OldWorld;
	TWeakObjectPtr<ACommandGameState> OldState;
	TWeakObjectPtr<AArmyGroup> Friendly;
	TWeakObjectPtr<AArmyGroup> Enemy;
};
bool FArmyMatchVictoryTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyMatchScenario(this, true));
	return true;
}
bool FArmyMatchDefeatTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyMatchScenario(this, false));
	return true;
}
#endif
