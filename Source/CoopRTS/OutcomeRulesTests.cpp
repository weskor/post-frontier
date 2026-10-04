#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/OutcomePolicy.h"
#include "CommandGameState.h" // EMatchResult is declared in this pinned header; no actors are instantiated.

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutcomeLostTest, "CoopRTS.Rules.Outcome.LostAndTies",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOutcomeLostTest::RunTest(const FString& Parameters)
{
	// A side is lost only when its HQ's lifecycle says so (a completed hold), never merely at 0 HP.
	const auto ExpectOutcome = [this](const TCHAR* What, bool bFriendlyLost, bool bEnemyLost, EMatchResult Expected) {
		const EMatchResult Result = OutcomePolicy::Evaluate({ bFriendlyLost, bEnemyLost,
			EMatchResult::Ongoing, EMatchResult::Victory, EMatchResult::Defeat });
		TestEqual(What, static_cast<int32>(Result), static_cast<int32>(Expected));
	};
	ExpectOutcome(TEXT("Neither HQ lost keeps the match ongoing"), false, false, EMatchResult::Ongoing);
	ExpectOutcome(TEXT("Only the enemy HQ lost wins"), false, true, EMatchResult::Victory);
	ExpectOutcome(TEXT("Only the friendly HQ lost loses"), true, false, EMatchResult::Defeat);
	ExpectOutcome(TEXT("Both lost in the same frame is defeat"), true, true, EMatchResult::Defeat);
	return true;
}

#endif
