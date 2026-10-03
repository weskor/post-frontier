#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ConstructionScenario.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructionLifecycleTest, "CoopRTS.Construction.Lifecycle",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FConstructionProductionTest, "CoopRTS.Construction.Production",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FConstructionLifecycleTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ConstructionScenarioTests::FConstructionScenario(this, false));
	return true;
}
bool FConstructionProductionTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(ConstructionScenarioTests::FConstructionScenario(this, true));
	return true;
}
#endif
