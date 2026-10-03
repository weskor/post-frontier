#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/PingPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPingThrottleTest, "CoopRTS.Rules.Pings.Throttle",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FPingThrottleTest::RunTest(const FString& Parameters)
{
	PingPolicy::FThrottle First;
	PingPolicy::FThrottle Second;
	TestTrue(TEXT("A player's first ping is accepted even at zero"), First.Accept(0.));
	TestFalse(TEXT("Same-frame duplicate is throttled"), First.Accept(0.));
	TestFalse(TEXT("Just before two seconds is throttled"), First.Accept(1.999));
	TestTrue(TEXT("Another player has an independent budget"), Second.Accept(1.));
	TestTrue(TEXT("The boundary opens exactly two seconds after acceptance, not after rejection"), First.Accept(2.));
	TestFalse(TEXT("New acceptance starts the next two-second window"), First.Accept(3.999));
	TestTrue(TEXT("Repeated windows accept their exact boundary"), First.Accept(4.));
	TestFalse(TEXT("Second player still has its own cooldown"), Second.Accept(2.999));
	TestTrue(TEXT("Second player's independent boundary opens"), Second.Accept(3.));
	First = PingPolicy::FThrottle();
	TestTrue(TEXT("A fresh battle resets the player's throttle"), First.Accept(0.));
	return true;
}
#endif
