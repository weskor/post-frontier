#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/AnnouncerPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerThrottleTest, "CoopRTS.Rules.Announcer.Throttle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerThrottleTest::RunTest(const FString& Parameters)
{
	const FName Attack(TEXT("own_hq_under_attack"));
	AnnouncerPolicy::FThrottle Throttle;
	TestTrue(TEXT("Force A starts the episode"), Throttle.Accept(Attack, 100, 0, 1, 10.));
	TestTrue(TEXT("Force B speaks immediately"), Throttle.Accept(Attack, 100, 0, 2, 11.));
	TestFalse(TEXT("Force A stays suppressed after B"), Throttle.Accept(Attack, 100, 0, 1, 12.));
	TestFalse(TEXT("Force B stays suppressed after A"), Throttle.Accept(Attack, 100, 0, 2, 13.));
	TestFalse(TEXT("Sustained damage does not reopen A after twenty seconds"), Throttle.Accept(Attack, 100, 0, 1, 31.));
	TestFalse(TEXT("Sustained damage does not reopen B"), Throttle.Accept(Attack, 100, 0, 2, 32.));
	TestTrue(TEXT("Same force number under another commander is new"), Throttle.Accept(Attack, 100, 1, 1, 33.));
	TestFalse(TEXT("Previously announced commander remains suppressed"), Throttle.Accept(Attack, 100, 0, 1, 34.));
	TestTrue(TEXT("A half transition takes priority for a new force"), Throttle.Accept(TEXT("own_hq_half"), 100, 0, 3, 35.));
	TestFalse(TEXT("A threshold hit records its new force"), Throttle.Accept(Attack, 100, 0, 3, 36.));
	TestFalse(TEXT("A threshold hit does not clear other announced forces"), Throttle.Accept(Attack, 100, 0, 2, 37.));
	TestTrue(TEXT("An announced force can deliver a critical transition"), Throttle.Accept(TEXT("own_hq_critical"), 100, 0, 1, 38.));
	TestFalse(TEXT("A critical transition does not reopen ordinary damage"), Throttle.Accept(Attack, 100, 0, 1, 39.));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerEpisodeResetTest, "CoopRTS.Rules.Announcer.EpisodeReset",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerEpisodeResetTest::RunTest(const FString& Parameters)
{
	const FName Attack(TEXT("own_hq_under_attack"));
	AnnouncerPolicy::FThrottle Throttle;
	TestTrue(TEXT("Initial force speaks"), Throttle.Accept(Attack, 100, 0, 1, 0.));
	TestTrue(TEXT("Second force is remembered"), Throttle.Accept(Attack, 100, 0, 2, 1.));
	for (const double Time : { 19., 38., 57., 76., 95. })
		TestFalse(FString::Printf(TEXT("Suppressed damage at %.0f renews the episode"), Time),
			Throttle.Accept(Attack, 100, 0, 1, Time));
	TestTrue(TEXT("Exactly twenty seconds quiet permits the same attacker"), Throttle.Accept(Attack, 100, 0, 1, 115.));
	TestTrue(TEXT("Quiet reset clears every previously announced force"), Throttle.Accept(Attack, 100, 0, 2, 116.));
	TestFalse(TEXT("Remembered forces stay suppressed in the new episode"), Throttle.Accept(Attack, 100, 0, 2, 117.));
	TestTrue(TEXT("A threshold after quiet starts a fresh episode"), Throttle.Accept(TEXT("own_hq_half"), 100, 0, 3, 137.));
	TestFalse(TEXT("Quiet threshold records its attacker"), Throttle.Accept(Attack, 100, 0, 3, 138.));
	TestTrue(TEXT("Quiet threshold clears the previous episode's attackers"), Throttle.Accept(Attack, 100, 0, 1, 139.));

	TestTrue(TEXT("Boundary structure starts an episode"), Throttle.Accept(Attack, 200, 0, 1, 100.));
	TestFalse(TEXT("Less than twenty seconds quiet does not reset"), Throttle.Accept(Attack, 200, 0, 1, 119.999));
	TestFalse(TEXT("Rejected boundary hit renews LastDamage"), Throttle.Accept(Attack, 200, 0, 1, 120.));
	TestTrue(TEXT("Exact boundary structure starts an episode"), Throttle.Accept(Attack, 300, 0, 1, 100.));
	TestTrue(TEXT("Exactly twenty seconds resets without a force change"), Throttle.Accept(Attack, 300, 0, 1, 120.));
	TestTrue(TEXT("Beyond-boundary structure starts an episode"), Throttle.Accept(Attack, 400, 0, 1, 100.));
	TestTrue(TEXT("More than twenty seconds quiet resets"), Throttle.Accept(Attack, 400, 0, 1, 120.001));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerStructureEpisodesTest, "CoopRTS.Rules.Announcer.StructureEpisodes",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerStructureEpisodesTest::RunTest(const FString& Parameters)
{
	const FName Attack(TEXT("own_hq_under_attack"));
	AnnouncerPolicy::FThrottle Throttle;
	TestTrue(TEXT("First structure speaks"), Throttle.Accept(Attack, 100, 0, 1, 10.));
	TestTrue(TEXT("Same event and attacker speak for another structure"), Throttle.Accept(Attack, 200, 0, 1, 10.));
	TestTrue(TEXT("New force speaks on the second structure"), Throttle.Accept(Attack, 200, 0, 2, 11.));
	TestTrue(TEXT("The force is still new on the first structure"), Throttle.Accept(Attack, 100, 0, 2, 11.));
	TestFalse(TEXT("Damage refreshes only the affected structure"), Throttle.Accept(Attack, 100, 0, 1, 29.999));
	TestTrue(TEXT("Another structure resets after its own quiet interval"), Throttle.Accept(Attack, 200, 0, 1, 31.));
	TestFalse(TEXT("Resetting another structure does not clear this episode"), Throttle.Accept(Attack, 100, 0, 2, 31.));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerStateTransitionsTest, "CoopRTS.Rules.Announcer.StateTransitions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerStateTransitionsTest::RunTest(const FString& Parameters)
{
	const FName Attack(TEXT("own_hq_under_attack"));
	for (const TCHAR* Name : { TEXT("own_hq_half"), TEXT("own_hq_critical"), TEXT("own_hq_offline"),
			 TEXT("enemy_hq_half"), TEXT("enemy_hq_critical"), TEXT("enemy_hq_offline") })
	{
		AnnouncerPolicy::FThrottle Throttle;
		const FName Id(Name);
		TestTrue(TEXT("Episode starts before transition"), Throttle.Accept(Attack, 100, 0, 1, 0.));
		TestTrue(FString::Printf(TEXT("%s speaks during damage"), Name), Throttle.Accept(Id, 100, 0, 2, 19.));
		TestTrue(FString::Printf(TEXT("%s allows simultaneous transitions"), Name), Throttle.Accept(Id, 100, 0, 2, 19.));
		TestTrue(TEXT("Threshold-only damage keeps renewing the episode"), Throttle.Accept(Id, 100, 0, 2, 38.));
		TestFalse(TEXT("Threshold damage keeps the earlier force suppressed"), Throttle.Accept(Attack, 100, 0, 1, 39.));
		TestFalse(TEXT("Threshold damage remembers its new force"), Throttle.Accept(Attack, 100, 0, 2, 40.));
	}
	for (const TCHAR* Name : { TEXT("region_captured"), TEXT("region_lost"), TEXT("drill_rig_lost") })
	{
		AnnouncerPolicy::FThrottle Throttle;
		const FName Id(Name);
		TestTrue(TEXT("Episode starts before non-damage transition"), Throttle.Accept(Attack, 100, 0, 1, 0.));
		TestTrue(FString::Printf(TEXT("%s speaks during damage"), Name), Throttle.Accept(Id, 100, 0, 2, 19.));
		TestTrue(FString::Printf(TEXT("%s allows simultaneous transitions"), Name), Throttle.Accept(Id, 100, 0, 2, 19.));
		TestTrue(TEXT("Non-damage transitions do not renew a damage episode"), Throttle.Accept(Attack, 100, 0, 1, 20.));
		TestTrue(TEXT("Non-damage transitions do not remember attacking forces"), Throttle.Accept(Attack, 100, 0, 2, 21.));
	}
	AnnouncerPolicy::FThrottle Throttle;
	TestFalse(TEXT("Unknown events are rejected"), Throttle.Accept(TEXT("unknown"), 100, 0, 1, 0.));
	TestTrue(TEXT("An unknown event does not record its attacker"), Throttle.Accept(Attack, 100, 0, 1, 0.));
	TestFalse(TEXT("Unknown event during an episode stays rejected"), Throttle.Accept(TEXT("unknown"), 100, 0, 2, 19.));
	TestTrue(TEXT("Unknown events do not renew a damage episode"), Throttle.Accept(Attack, 100, 0, 1, 20.));
	TestTrue(TEXT("Unknown events do not record a new attacker"), Throttle.Accept(Attack, 100, 0, 2, 21.));
	return true;
}
#endif
