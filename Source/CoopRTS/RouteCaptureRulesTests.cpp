#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/RouteCapturePolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRouteCaptureRulesTest, "CoopRTS.Rules.RouteCapture",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRouteCaptureRulesTest::RunTest(const FString& Parameters)
{
	using namespace RouteCapturePolicy;
	// 0 - 1 - 2 is the route from 0 to 2. Region 3 touches 0 and 1 but is not on it; 4 is cut off.
	const uint64 Graph[] = { 0b01010, 0b01101, 0b00010, 0b00011, 0 };
	const auto Standing = [](int32 Source, int32 Origin, int32 Applied) {
		FMarch March;
		March.Source = Source;
		March.Target = 2;
		March.Origin = Origin;
		March.Applied = Applied;
		March.bHasAnchor = true;
		return March;
	};
	const auto Check = [&](const TCHAR* What, const FMarch& March, EStep Step, int32 Origin) {
		const FDecision Decision = Decide(Graph, 5, March);
		TestTrue(What, Decision.Step == Step);
		TestEqual(*FString::Printf(TEXT("%s: route origin"), What), Decision.Origin, Origin);
	};

	Check(TEXT("A fresh order secures the uncontrolled region it starts in"), Standing(0, INDEX_NONE, INDEX_NONE), EStep::Secure, 0);
	Check(TEXT("A route region already walked to is secured while uncontrolled"), Standing(1, 0, 1), EStep::Secure, 1);
	FMarch Controlled = Standing(1, 0, 1);
	Controlled.bControlled = true;
	Check(TEXT("A controlled route region advances"), Controlled, EStep::Advance, 1);
	Check(TEXT("The target itself advances to its own completion rules"), Standing(2, 1, 2), EStep::Advance, 2);
	FMarch NoAnchor = Standing(1, 0, 1);
	NoAnchor.bHasAnchor = false;
	Check(TEXT("A region without a capture point advances"), NoAnchor, EStep::Advance, 1);

	FMarch Contested = Standing(1, 0, INDEX_NONE);
	Contested.bContested = true;
	Check(TEXT("A contested route region with no waypoint yet is secured"), Contested, EStep::Secure, 1);
	Contested.Applied = 1;
	Check(TEXT("A contested route region is held until the waypoint is physically reached"), Contested, EStep::Secure, 1);
	Contested.bArrived = true;
	Check(TEXT("A contested route region is passed after physical arrival"), Contested, EStep::Advance, 1);
	Contested.bArrived = false;
	Contested.Applied = 2;
	Check(TEXT("A contested route region is passed when walking on to another waypoint"), Contested, EStep::Advance, 1);

	Check(TEXT("Off-route ground is not captured; the force keeps walking"), Standing(3, 0, 1), EStep::Continue, 0);
	FMarch OffContested = Standing(3, 0, 1);
	OffContested.bContested = true;
	Check(TEXT("Off-route contested ground keeps walking too"), OffContested, EStep::Continue, 0);
	FMarch OffControlled = Standing(3, 0, 1);
	OffControlled.bControlled = true;
	Check(TEXT("Off-route controlled ground does not re-plan the route from underfoot"), OffControlled, EStep::Continue, 0);
	Check(TEXT("Back on the route the next region is secured and becomes the origin"), Standing(1, 0, 1), EStep::Secure, 1);
	Check(TEXT("The origin region is still on its own route"), Standing(0, 0, 1), EStep::Secure, 0);
	Check(TEXT("Without an applied waypoint the standing region re-roots the route"), Standing(3, 0, INDEX_NONE), EStep::Secure, 3);
	Check(TEXT("A route to the target that does not exist re-roots rather than freezing"), Standing(3, 4, 1), EStep::Secure, 3);
	return true;
}
#endif
