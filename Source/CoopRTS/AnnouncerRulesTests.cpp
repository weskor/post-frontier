#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Rules/AnnouncerPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerThrottleTest, "CoopRTS.Rules.Announcer.Throttle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerThrottleTest::RunTest(const FString& Parameters)
{
	const FName Attack(TEXT("own_hq_under_attack"));
	AnnouncerPolicy::FThrottle Throttle;
	TestTrue(TEXT("Force A starts the episode"), Throttle.Accept(Attack, 100, 0, 1, 0, 10.));
	TestTrue(TEXT("Force B speaks immediately"), Throttle.Accept(Attack, 100, 0, 2, 0, 11.));
	TestFalse(TEXT("Force A stays suppressed after B"), Throttle.Accept(Attack, 100, 0, 1, 0, 12.));
	TestFalse(TEXT("Force B stays suppressed after A"), Throttle.Accept(Attack, 100, 0, 2, 0, 13.));
	TestFalse(TEXT("Sustained damage does not reopen A after twenty seconds"), Throttle.Accept(Attack, 100, 0, 1, 0, 31.));
	TestFalse(TEXT("Sustained damage does not reopen B"), Throttle.Accept(Attack, 100, 0, 2, 0, 32.));
	TestTrue(TEXT("Same force number under another commander is new"), Throttle.Accept(Attack, 100, 1, 1, 0, 33.));
	TestFalse(TEXT("Previously announced commander remains suppressed"), Throttle.Accept(Attack, 100, 0, 1, 0, 34.));
	TestTrue(TEXT("New tier speaks immediately for an announced force"), Throttle.Accept(Attack, 100, 0, 1, 1, 35.));
	TestFalse(TEXT("Tier change does not clear announced forces"), Throttle.Accept(Attack, 100, 0, 2, 1, 36.));
	TestFalse(TEXT("Returning to an announced tier is suppressed"), Throttle.Accept(Attack, 100, 0, 1, 0, 37.));
	TestTrue(TEXT("A hit introducing both a force and tier is accepted"), Throttle.Accept(Attack, 100, 0, 3, 2, 38.));
	TestFalse(TEXT("The combined hit records its new tier"), Throttle.Accept(Attack, 100, 0, 1, 2, 39.));
	TestFalse(TEXT("The combined hit records its new force"), Throttle.Accept(Attack, 100, 0, 3, 0, 40.));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerEpisodeResetTest, "CoopRTS.Rules.Announcer.EpisodeReset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerEpisodeResetTest::RunTest(const FString& Parameters)
{
	const FName Attack(TEXT("own_hq_under_attack"));
	AnnouncerPolicy::FThrottle Throttle;
	TestTrue(TEXT("Initial force speaks"), Throttle.Accept(Attack, 100, 0, 1, 0, 0.));
	TestTrue(TEXT("Second force is remembered"), Throttle.Accept(Attack, 100, 0, 2, 0, 1.));
	TestTrue(TEXT("Second tier is remembered"), Throttle.Accept(Attack, 100, 0, 1, 1, 2.));
	for (const double Time : { 19., 38., 57., 76., 95. })
		TestFalse(FString::Printf(TEXT("Suppressed damage at %.0f renews the episode"), Time),
			Throttle.Accept(Attack, 100, 0, 1, 0, Time));
	TestTrue(TEXT("Exactly twenty seconds quiet permits the same attacker and tier"),
		Throttle.Accept(Attack, 100, 0, 1, 0, 115.));
	TestTrue(TEXT("Quiet reset clears previously announced tiers"), Throttle.Accept(Attack, 100, 0, 1, 1, 116.));
	TestTrue(TEXT("Quiet reset independently clears previously announced forces"), Throttle.Accept(Attack, 100, 0, 2, 1, 117.));
	TestFalse(TEXT("Both sets persist again in the new episode"), Throttle.Accept(Attack, 100, 0, 2, 0, 118.));

	TestTrue(TEXT("Boundary structure starts an episode"), Throttle.Accept(Attack, 200, 0, 1, 0, 100.));
	TestFalse(TEXT("Less than twenty seconds quiet does not reset"), Throttle.Accept(Attack, 200, 0, 1, 0, 119.999));
	TestFalse(TEXT("Rejected boundary hit renews LastDamage"), Throttle.Accept(Attack, 200, 0, 1, 0, 120.));
	TestTrue(TEXT("Exact boundary structure starts an episode"), Throttle.Accept(Attack, 300, 0, 1, 0, 100.));
	TestTrue(TEXT("Exactly twenty seconds resets without a tier or force change"), Throttle.Accept(Attack, 300, 0, 1, 0, 120.));
	TestTrue(TEXT("Beyond-boundary structure starts an episode"), Throttle.Accept(Attack, 400, 0, 1, 0, 100.));
	TestTrue(TEXT("More than twenty seconds quiet resets"), Throttle.Accept(Attack, 400, 0, 1, 0, 120.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerStructureEpisodesTest, "CoopRTS.Rules.Announcer.StructureEpisodes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerStructureEpisodesTest::RunTest(const FString& Parameters)
{
	const FName Attack(TEXT("own_hq_under_attack"));
	AnnouncerPolicy::FThrottle Throttle;
	TestTrue(TEXT("First structure speaks"), Throttle.Accept(Attack, 100, 0, 1, 0, 10.));
	TestTrue(TEXT("Same event and attacker speak for another structure"), Throttle.Accept(Attack, 200, 0, 1, 0, 10.));
	TestTrue(TEXT("New force speaks on the second structure"), Throttle.Accept(Attack, 200, 0, 2, 0, 11.));
	TestTrue(TEXT("The force is still new on the first structure"), Throttle.Accept(Attack, 100, 0, 2, 0, 11.));
	TestTrue(TEXT("New tier speaks on the first structure"), Throttle.Accept(Attack, 100, 0, 1, 1, 12.));
	TestTrue(TEXT("The tier is still new on the second structure"), Throttle.Accept(Attack, 200, 0, 1, 1, 12.));
	TestFalse(TEXT("Damage refreshes only the affected structure"), Throttle.Accept(Attack, 100, 0, 1, 0, 29.999));
	TestTrue(TEXT("Another structure resets after its own quiet interval"), Throttle.Accept(Attack, 200, 0, 1, 0, 32.));
	TestFalse(TEXT("Resetting another structure does not clear this episode"), Throttle.Accept(Attack, 100, 0, 2, 1, 32.));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerStateTransitionsTest, "CoopRTS.Rules.Announcer.StateTransitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerStateTransitionsTest::RunTest(const FString& Parameters)
{
	const FName Attack(TEXT("own_hq_under_attack"));
	AnnouncerPolicy::FThrottle Throttle;
	TestFalse(TEXT("Unknown events are rejected"), Throttle.Accept(TEXT("unknown"), 100, 0, 1, 0, 0.));
	TestTrue(TEXT("An unknown event does not record its attacker or tier"), Throttle.Accept(Attack, 100, 0, 1, 0, 0.));
	for (const AnnouncerPolicy::FDefinition& Definition : AnnouncerPolicy::Definitions())
	{
		if (!Definition.bStateChange)
			continue;
		const FName Id(Definition.Id);
		TestTrue(FString::Printf(TEXT("%s speaks during an attack episode"), Definition.Id), Throttle.Accept(Id, 100, 0, 1, 2, 19.));
		TestTrue(FString::Printf(TEXT("%s allows simultaneous transitions"), Definition.Id), Throttle.Accept(Id, 100, 0, 1, 2, 19.));
	}
	TestTrue(TEXT("State transitions do not renew a damage episode"), Throttle.Accept(Attack, 100, 0, 1, 0, 20.));
	TestTrue(TEXT("Unknown-event structure starts an episode"), Throttle.Accept(Attack, 200, 0, 1, 0, 0.));
	TestFalse(TEXT("Unknown event during an episode stays rejected"), Throttle.Accept(TEXT("unknown"), 200, 0, 1, 0, 19.));
	TestTrue(TEXT("Unknown events do not renew a damage episode"), Throttle.Accept(Attack, 200, 0, 1, 0, 20.));
	return true;
}
#endif
