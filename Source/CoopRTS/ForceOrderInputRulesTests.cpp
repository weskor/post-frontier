#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ForceOrderInput.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceOrderInputRulesTest, "CoopRTS.Rules.ForceOrderInput",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FForceOrderInputRulesTest::RunTest(const FString& Parameters)
{
	using namespace ForceOrderInput;
	const uint64 Graph[] = { 1ull << 1, (1ull << 0) | (1ull << 2), 1ull << 1, 0 };
	FForce Selection[] = { { true, 0, 1 }, { true, 2, 2 } };
	FContext Context;
	Context.Graph = MakeArrayView(Graph);
	Context.Forces = MakeArrayView(Selection);
	Context.TargetRegion = 1;
	Context.bAvailable = true;
	TestTrue(TEXT("Several forces smart-click a region as Move & Hold regardless of control"), Resolve(Context).Resolution == EResolution::MoveHold);
	Context.bHostileStructure = true;
	TestTrue(TEXT("Hostile structure smart-click attacks"), Resolve(Context).Resolution == EResolution::Attack);
	Context.bHostileStructure = false;
	Context.bAttack = true;
	TestTrue(TEXT("A mode explicitly attacks a region"), Resolve(Context).Resolution == EResolution::Attack);
	Context.bQueue = true;
	TestTrue(TEXT("One vacant queue slot on every force accepts"), Resolve(Context).IsAllowed());
	Selection[1].OrderCount = 3;
	TestTrue(TEXT("Any full queue rejects the entire selection"), Resolve(Context).Rejection == ERejection::QueueFull);
	Context.bQueue = false;
	TestTrue(TEXT("Replacing a full queue is allowed"), Resolve(Context).IsAllowed());
	Selection[1].bOwned = false;
	TestTrue(TEXT("Foreign member rejects atomic selection"), Resolve(Context).Rejection == ERejection::NotOwned);
	Selection[1].bOwned = true;
	Context.TargetRegion = 3;
	TestTrue(TEXT("Disconnected target is rejected"), Resolve(Context).Rejection == ERejection::Unreachable);
	Context.TargetRegion = INDEX_NONE;
	TestTrue(TEXT("HUD or outside-map target explains rejection"), Resolve(Context).Rejection == ERejection::InvalidTarget);
	Context.TargetRegion = 4;
	TestTrue(TEXT("Region outside graph is rejected"), Resolve(Context).Rejection == ERejection::InvalidTarget);
	Context.TargetRegion = 1;
	Context.Forces = {};
	Context.bProducerSelected = true;
	Context.ProducerRegion = 0;
	TestTrue(TEXT("A without forces cannot become a rally command"), Resolve(Context).Rejection == ERejection::NoSelection);
	Context.bAttack = false;
	TestTrue(TEXT("Production building alone smart-clicks a rally region"), Resolve(Context).Resolution == EResolution::Rally);
	Context.TargetRegion = 3;
	TestTrue(TEXT("Disconnected rally rejects"), Resolve(Context).Rejection == ERejection::Unreachable);
	Context.bProducerSelected = false;
	TestTrue(TEXT("No commandable selection rejects"), Resolve(Context).Rejection == ERejection::NoSelection);
	Context.bAvailable = false;
	TestTrue(TEXT("Ended match cannot give orders"), Resolve(Context).Rejection == ERejection::Unavailable);
	return true;
}
#endif
