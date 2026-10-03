#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ForceCardPolicy.h"
#include <limits>

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceCardETATest, "CoopRTS.Rules.ForceCardETA",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FForceCardETATest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("A 6000cm route at the slowest 300cm/s takes twenty seconds"), ForceCardPolicy::TravelSeconds(6000., 300.f), 20);
	TestEqual(TEXT("A partial second rounds up rather than promising early arrival"), ForceCardPolicy::TravelSeconds(6001., 300.f), 21);
	TestEqual(TEXT("An arrived force has zero travel time"), ForceCardPolicy::TravelSeconds(0., 300.f), 0);
	TestEqual(TEXT("A stationary force has unavailable ETA, not zero"), ForceCardPolicy::TravelSeconds(6000., 0.f), INDEX_NONE);
	TestEqual(TEXT("An invalid path has unavailable ETA"), ForceCardPolicy::TravelSeconds(-1., 300.f), INDEX_NONE);
	TestEqual(TEXT("A nonfinite speed has unavailable ETA"), ForceCardPolicy::TravelSeconds(6000., std::numeric_limits<float>::infinity()), INDEX_NONE);
	TestEqual(TEXT("Long travel saturates without integer overflow"), ForceCardPolicy::TravelSeconds(1.e30, 1.f), MAX_int32);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceCardStateTest, "CoopRTS.Rules.ForceCardState",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FForceCardStateTest::RunTest(const FString& Parameters)
{
	using namespace ForceCardPolicy;
	TestEqual(TEXT("Automatic Attack recovery keeps the withdrawal presentation"),
		ResolveState(EState::Refilling, true, false, 5), EState::Withdrawing);
	TestEqual(TEXT("Manual Retreat refill never borrows a retained Attack resume count"),
		ResolveState(EState::Refilling, false, false, 5), EState::Refilling);
	TestEqual(TEXT("Refill without a resume count is not automatic withdrawal"),
		ResolveState(EState::Refilling, true, false, 0), EState::Refilling);
	TestEqual(TEXT("Region response takes precedence over passive Holding"),
		ResolveState(EState::Holding, false, true, 0), EState::Responding);
	TestEqual(TEXT("A quiet holder has no lingering response label"),
		ResolveState(EState::Holding, false, false, 0), EState::Holding);
	TestEqual(TEXT("A marching Attack is not labelled withdrawing merely because it retains a resume count"),
		ResolveState(EState::Marching, true, false, 5), EState::Marching);
	return true;
}
#endif
