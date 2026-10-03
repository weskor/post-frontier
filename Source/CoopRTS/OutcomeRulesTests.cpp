#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/OutcomePolicy.h"
#include "CommandGameState.h" // EMatchResult is declared in this pinned header; no actors are instantiated.

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOutcomeHealthTest, "CoopRTS.Rules.Outcome.HealthAndTies",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FOutcomeHealthTest::RunTest(const FString& Parameters)
{
	const auto ExpectOutcome = [this](const TCHAR* What, int32 FriendlyHealth, int32 EnemyHealth, EMatchResult Expected) {
		const EMatchResult Result = OutcomePolicy::Evaluate({ FriendlyHealth, EnemyHealth,
			EMatchResult::Ongoing, EMatchResult::Victory, EMatchResult::Defeat });
		TestEqual(What, static_cast<int32>(Result), static_cast<int32>(Expected));
	};
	ExpectOutcome(TEXT("Both headquarters alive keeps match ongoing"), 1, 1, EMatchResult::Ongoing);
	ExpectOutcome(TEXT("Only enemy headquarters destroyed wins"), 1, 0, EMatchResult::Victory);
	ExpectOutcome(TEXT("Only friendly headquarters destroyed loses"), 0, 1, EMatchResult::Defeat);
	ExpectOutcome(TEXT("Simultaneous destruction is defeat"), 0, 0, EMatchResult::Defeat);
	ExpectOutcome(TEXT("Enemy health below zero remains destroyed"), 1, -1, EMatchResult::Victory);
	ExpectOutcome(TEXT("Friendly health below zero still takes precedence"), -1, 0, EMatchResult::Defeat);
	return true;
}

#endif
