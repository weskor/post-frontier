#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "PlanningFixture.h"

// A restart after a finished battle travels to a fresh world, which opens in planning again: frozen, a kit slot for
// the carried commander, the opening wallet and a new sixty-second countdown.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningRestartWorldTest, "CoopRTS.Planning.Restart",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PlanningRestartTests
{
class FScenario final : public PlanningFixture::FScenario
{
public:
	using PlanningFixture::FScenario::FScenario;

private:
	bool Step() override
	{
		switch (Stage)
		{
		case 0:
			return Finish();
		case 1:
			return Reopened();
		default:
			return Started();
		}
	}

	bool Finish()
	{
		if (State->Planning.Kits.Num() != 2)
			return false;
		OldWorld = World;
		RemoveCommander(GuestPtr.Get());
		OldMap = World->GetOutermost()->GetName();
		State->SetMatchResult(EMatchResult::Defeat);
		if (!Check(FCommandService::Restart(PC).IsAccepted(), TEXT("Restart is accepted after the battle")))
			return Done();
		Enter(1);
		return false;
	}

	bool Reopened()
	{
		if (!World || World == OldWorld.Get() || World->GetOutermost()->GetName() != OldMap || !State || !Host || Host->CommanderIndex < 0
			|| State->Planning.Kits.IsEmpty())
			return false;
		if (!Check(State->IsPlanning() && World->IsPaused() && State->GetPlanningEndCount() == 0,
				FString::Printf(TEXT("The restarted battle opens in planning, frozen (planning %d, paused %d, ended %d)"), State->IsPlanning(),
					World->IsPaused(), State->GetPlanningEndCount()))
			|| !Check(State->Planning.Kits.Num() == 1 && State->Planning.Kits[0].Commander == Host && !State->Planning.Kits[0].bReady,
				FString::Printf(TEXT("The carried commander has a fresh, unready kit slot (kits %d, host kit %d, ready %d, second %s slot %d)"),
					State->Planning.Kits.Num(), State->Planning.Kits[0].Commander == Host, State->Planning.Kits[0].bReady,
					State->Planning.Kits.Num() > 1 && State->Planning.Kits[1].Commander ? *State->Planning.Kits[1].Commander->GetName() : TEXT("none"),
					State->Planning.Kits.Num() > 1 && State->Planning.Kits[1].Commander ? State->Planning.Kits[1].Commander->CommanderIndex : -9))
			|| !Check(Host->Resources == 200 && Host->Data == 0, TEXT("The opening wallet is 200 Power and 0 Data"))
			|| !Check(State->Planning.SecondsRemaining > 0.f && State->Planning.SecondsRemaining <= 60.f, TEXT("A new countdown runs")))
			return Done();
		if (!Check(FPlanningCommands::SetReady(Host, true).IsAccepted(), TEXT("The only human can be Ready")))
			return Done();
		Enter(2);
		return false;
	}

	bool Started()
	{
		if (State->IsPlanning())
			return false;
		const float Match = State->GetBattleClockStartServerTime();
		Check(State->GetPlanningEndCount() == 1 && State->GetPlanningEnd() == EPlanningEnd::AllReady && !World->IsPaused() && Match >= 0.f,
			TEXT("The lone human's Ready ends the restarted planning exactly once"));
		return Done();
	}

	TWeakObjectPtr<UWorld> OldWorld;
	FString OldMap;
};
}

bool FPlanningRestartWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PlanningRestartTests::FScenario(this));
	return true;
}

#endif
