#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "DepositSite.h"
#include "Headquarters.h"

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
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetNetMode() != NM_Client)
			{
				World = Context.World();
				break;
			}
		if (!World)
			return false;
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!State || !PC || !Wallet || !State->FriendlyHeadquarters || !State->EnemyHeadquarters)
			return false;
		if (Stage == 0)
		{
			if (World->GetTimeSeconds() < 3.f || !ArmyTestSetup::CombatActors(World))
				return false;
			State->bVerificationIncomePaused = true;
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
			{
				if (It->GetTeamIndex() == 5)
					Enemy = *It;
				else if (It->GetOwningPlayerState() == Wallet && It->GetArmyIndex() == 0)
					Friendly = *It;
			}
			if (!Friendly.IsValid() || !Enemy.IsValid())
				return Fail(TEXT("Live outcome fixture armies missing"));
			ArmyTestSetup::Research(PC, EArmyDoctrine::SiegeOptics);
			if (Wallet->Doctrine != EArmyDoctrine::SiegeOptics)
				return Fail(TEXT("Old match must own a paid specialization"));
			AHeadquarters* Target = bVictory ? State->EnemyHeadquarters : State->FriendlyHeadquarters;
			AArmyGroup* Attacker = bVictory ? Friendly.Get() : Enemy.Get();
			Target->Health = 80; // Short encounter fixture; real weapons deliver every subsequent hit.
			for (AArmyUnit* Unit : Attacker->GetUnits())
			{
				Unit->SetActorLocation(Target->GetActorLocation() + FVector(-650.f, Unit->GetCompositionSlot() * 100.f, 0.f),
					false, nullptr, ETeleportType::TeleportPhysics);
				BeforeShots += Unit->AttackCount;
			}
			if (bVictory)
				PC->ServerIssueAttack(Attacker, Target->GetActorLocation(), Target);
			else
				Attacker->IssueAttack(Target->GetActorLocation(), Target);
			if (Attacker->Order != EArmyOrder::Attack || Attacker->AttackTarget != Target)
				return Fail(TEXT("A targeted HQ attack must be accepted without a shield prerequisite"));
			OldState = State;
			OldWorld = World;
			Stage = 1;
			Test->AddInfo(TEXT("Real HQ attack in progress; waiting for weapon-caused outcome."));
			return false;
		}
		if (Stage == 1)
		{
			if (State->MatchResult == EMatchResult::Ongoing)
				return false;
			const EMatchResult Expected = bVictory ? EMatchResult::Victory : EMatchResult::Defeat;
			if (State->MatchResult != Expected || !Friendly.IsValid() || !Enemy.IsValid())
				return Fail(TEXT("Weapon encounter resolved to the wrong outcome"));
			const AArmyGroup* Attacker = bVictory ? Friendly.Get() : Enemy.Get();
			uint32 AfterShots = 0;
			for (const AArmyUnit* Unit : Attacker->GetUnits())
				AfterShots += Unit->AttackCount;
			if (AfterShots <= BeforeShots || (bVictory ? State->EnemyHeadquarters->Health : State->FriendlyHeadquarters->Health) != 0)
				return Fail(TEXT("Outcome requires real attacks and zero target HQ health"));
			const uint32 Serial = Friendly->OrderSerial;
			const int32 Balance = Wallet->Resources;
			const int32 Buildings = State->Buildings.Num();
			PC->ServerIssueOrder(Friendly.Get(), EArmyOrder::Move, ArmyTestSetup::FromFriendlyHQ(State, 1700.f, 2300.f, 5.f));
			PC->ServerPlaceBuilding(ArmyTestSetup::BarracksIndex, ArmyTestSetup::FromFriendlyHQ(State, 400.f, 0.f, 5.f));
			ArmyTestSetup::Research(PC, EArmyDoctrine::FieldRepairs);
			State->Tick(2.f);
			if (Friendly->OrderSerial != Serial || Wallet->Resources != Balance || State->Buildings.Num() != Buildings
				|| Wallet->Doctrine != EArmyDoctrine::SiegeOptics)
				return Fail(TEXT("Terminal match must reject orders/construction/research and stop income"));
			Slot = Wallet->CommanderIndex;
			PC->ServerRequestRestart();
			Stage = 2;
			Test->AddInfo(TEXT("Outcome and terminal guards observed; seamless fresh world requested."));
			return false;
		}
		if (World == OldWorld.Get() || State == OldState.Get() || Wallet->CommanderIndex < 0)
			return false;
		if (State->MatchResult != EMatchResult::Ongoing || Wallet->CommanderIndex != Slot
			|| Wallet->Doctrine != EArmyDoctrine::None
			|| Wallet->Resources < ACommandPlayerState::InitialResources
			|| Wallet->Resources > ACommandPlayerState::InitialResources + 2 * State->GetIncomePerSecond(Wallet)
			|| State->FriendlyHeadquarters->Health != State->FriendlyHeadquarters->MaxHealth()
			|| State->EnemyHeadquarters->Health != State->EnemyHeadquarters->MaxHealth()
			|| State->GetIncomePerSecond(Wallet) != ACommandGameState::BaselineIncomePerSecond)
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
				|| Deposit->Remaining != (Deposit->bRich ? 3000 : 2400))
				return Fail(TEXT("Fresh deposits must reset occupancy and finite reserves"));
		Test->AddInfo(TEXT("Real weapon outcome, terminal command/economy guards and fresh construction match restart passed."));
		return true;
	}
private:
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	FAutomationTestBase* Test;
	bool bVictory;
	int32 Stage = 0;
	int32 Slot = -1;
	uint32 BeforeShots = 0;
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
