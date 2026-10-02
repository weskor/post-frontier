#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Rules/AnnouncerPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FAnnouncerThrottleTest, "CoopRTS.Rules.Announcer.Throttle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FAnnouncerThrottleTest::RunTest(const FString& Parameters)
{
	AnnouncerPolicy::FThrottle Throttle;
	const FName Attack(TEXT("own_hq_under_attack"));
	TestFalse(TEXT("Unknown events are rejected"), Throttle.Accept(TEXT("unknown"), 0, 0, 1, 0, 0.));
	TestTrue(TEXT("First attack speaks"), Throttle.Accept(Attack, 0, 0, 1, 0, 10.));
	TestFalse(TEXT("Same attack is suppressed immediately"), Throttle.Accept(Attack, 0, 0, 1, 0, 10.001));
	TestFalse(TEXT("New force cannot interrupt the cooldown"), Throttle.Accept(Attack, 0, 0, 2, 0, 29.999));
	TestFalse(TEXT("New tier cannot interrupt the cooldown"), Throttle.Accept(Attack, 0, 0, 1, 1, 29.999));
	TestTrue(TEXT("Changed tier speaks at exact cooldown boundary"), Throttle.Accept(Attack, 0, 0, 1, 1, 30.));
	TestFalse(TEXT("Accepted tier keeps its identity after rejected changes"), Throttle.Accept(Attack, 0, 0, 1, 1, 49.999));
	TestTrue(TEXT("Same force number from a different commander is new"), Throttle.Accept(Attack, 0, 1, 1, 1, 50.));
	TestTrue(TEXT("New force from the same commander is new"), Throttle.Accept(Attack, 0, 1, 2, 1, 70.));
	TestFalse(TEXT("Same attacker and tier never repeat merely after cooldown"), Throttle.Accept(Attack, 0, 1, 2, 1, 100.));
	TestTrue(TEXT("A new tier after cooldown speaks"), Throttle.Accept(Attack, 0, 1, 2, 2, 100.));
	TestTrue(TEXT("Other HQ has independent cooldown"), Throttle.Accept(TEXT("enemy_hq_under_attack"), 5, 0, 1, 0, 10.));
	TestTrue(TEXT("Affected teams have independent cooldown"), Throttle.Accept(Attack, 5, 0, 1, 0, 10.));
	for (const AnnouncerPolicy::FDefinition& Definition : AnnouncerPolicy::Definitions())
	{
		if (!Definition.bStateChange)
			continue;
		const FName Id(Definition.Id);
		TestTrue(FString::Printf(TEXT("%s speaks inside attack cooldown"), Definition.Id), Throttle.Accept(Id, 0, 0, 1, 2, 10.));
		TestTrue(FString::Printf(TEXT("%s allows distinct simultaneous transitions"), Definition.Id), Throttle.Accept(Id, 0, 0, 1, 2, 10.));
	}
	return true;
}
#endif
