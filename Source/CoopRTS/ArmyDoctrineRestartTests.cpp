#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyDoctrineFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDoctrineRestartTest, "CoopRTS.Doctrine.Restart",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ArmyDoctrineRestartTests
{
using namespace ArmyDoctrineFixture;

class FRestartScenario final : public FDoctrineScenario
{
public:
	using FDoctrineScenario::FDoctrineScenario;
private:
	bool RestartCase() const override { return true; }
	bool Step(double Now) override
	{
		switch (Stage)
		{
		case 0:
			return Stage0(Now);
		case 1:
			return Stage1(Now);
		case 2:
			return Stage2();
		default:
			return true;
		}
	}

	bool Stage0(double Now)
	{
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::SiegeOptics);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics,
				TEXT("Old match owns a selected doctrine before the HQ assault")))
			return true;
		AHeadquarters* HQ = Actors.State->EnemyHeadquarters;
		AArmyUnit* Siege = Actors.Armies[0]->GetUnits()[4];
		if (!Check(IsValid(HQ) && Siege->IsAlive(), TEXT("Real siege and enemy HQ are available")))
			return true;
		HQ->Health = 1; // Only the weapon, never the setup, delivers the lethal hit.
		Siege->SetActorLocation(HQ->GetActorLocation() + FVector(150.f, 0.f, 0.f),
			false, nullptr, ETeleportType::TeleportPhysics);
		Siege->NextAttackTime = 0.f;
		const uint32 Before = Siege->AttackCount;
		Siege->FireAt(HQ);
		if (!Check(Siege->AttackCount == Before + 1 && HQ->Health == 0,
				TEXT("A real doctrine-bearing siege shot destroys the enemy HQ")))
			return true;
		Next(1, Now);
		return false;
	}

	bool Stage1(double Now)
	{
		if (Actors.State->MatchResult == EMatchResult::Ongoing)
			return false;
		if (!Check(Actors.State->MatchResult == EMatchResult::Victory,
				TEXT("Weapon-caused HQ destruction ends the match")))
			return true;
		const int32 TerminalBalance = Actors.Wallet->Resources;
		ArmyTestSetup::Research(Actors.Controller.Get(), EArmyDoctrine::EntrenchedFrontline);
		if (!Check(Actors.Wallet->Doctrine == EArmyDoctrine::SiegeOptics && Actors.Wallet->Resources == TerminalBalance,
				TEXT("Terminal research RPC neither replaces the purchase nor charges again")))
			return true;
		OldWorld = Actors.World;
		OldState = Actors.State;
		FCommandService::Restart(Actors.Controller.Get());
		Next(2, Now);
		return false;
	}

	bool Stage2()
	{
		UWorld* FreshWorld = StandaloneWorld();
		if (!FreshWorld || FreshWorld == OldWorld.Get())
			return false;
		ACommandPlayerController* FreshController = ArmyTestSetup::Controller(FreshWorld);
		ACommandGameState* FreshState = FreshWorld->GetGameState<ACommandGameState>();
		ACommandPlayerState* FreshWallet = FreshController ? FreshController->GetPlayerState<ACommandPlayerState>() : nullptr;
		// Seamless travel passes through a transition world that still carries the old GameState.
		if (!FreshState || FreshState == OldState.Get() || !FreshWallet
			|| FreshWallet->CommanderIndex < 0 || !ArmyTestSetup::MapReady(FreshState))
			return false;
		if (!Check(FreshState->MatchResult == EMatchResult::Ongoing && FreshWallet->Doctrine == EArmyDoctrine::None,
				*FString::Printf(TEXT("Fresh world clears the purchased research (result=%d doctrine=%d)"),
					static_cast<int32>(FreshState->MatchResult), static_cast<int32>(FreshWallet->Doctrine))))
			return true;
		for (TActorIterator<AArmyGroup> It(FreshWorld); It; ++It)
			if (!Check(It->GetOwningPlayerState() != FreshWallet, TEXT("Restart does not recreate fixed player armies")))
				return true;
		ArmyTestSetup::Research(FreshController, EArmyDoctrine::FieldRepairs);
		if (!Check(FreshWallet->Doctrine == EArmyDoctrine::FieldRepairs,
				TEXT("Fresh match permits a different paid workshop specialization")))
			return true;
		Test->AddInfo(TEXT("Real siege HQ kill, terminal doctrine rejection, fresh-world None reset, independent new selection."));
		return true;
	}
	TWeakObjectPtr<UWorld> OldWorld;
	TWeakObjectPtr<ACommandGameState> OldState;
};
}

bool FDoctrineRestartTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ArmyDoctrineRestartTests::FRestartScenario(this));
	return true;
}

#endif
