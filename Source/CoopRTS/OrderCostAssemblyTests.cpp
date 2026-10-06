#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyGroupPathing.h"
#include "SupplyDeliveryFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrderCostAssemblyTest, "CoopRTS.Forces.AssemblyCount",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace OrderCostAssembly
{
// A barracks that gets its force validates the rally route for a force with no members (the assembly check):
// that is an order with one synchronous path query, and the counter must see it. The counters start at zero when
// the scenario is built, before the fixture creates the producer.
class FScenario : public SupplyTests::FScenarioBase
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : FScenarioBase(InTest)
	{
		ArmyGroupPathing::Reset();
	}

protected:
	bool Run() override
	{
		const ArmyGroupPathing::FQueryStats Cost = ArmyGroupPathing::Snapshot();
		Test->AddInfo(FString::Printf(TEXT("Setting up the producer (its force's first order) %lld orders, %lld path queries in orders, %lld in all"), Cost.Orders, Cost.OrderPathQueries, Cost.PathQueries));
		Check(Cost.Orders >= 1, TEXT("The configuration planned an order"));
		Check(Cost.OrderPathQueries >= Cost.Orders, TEXT("Each assembly order is counted with its path query"));
		return true;
	}
};
}

bool FOrderCostAssemblyTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(OrderCostAssembly::FScenario(this));
	return true;
}
#endif
