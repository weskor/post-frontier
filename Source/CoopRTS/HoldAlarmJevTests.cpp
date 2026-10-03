#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldJevTest, "CoopRTS.Hold.Jev",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace HoldAlarmJevTests
{
class FScenario : public HoldAlarmFixture::FScenario
{
public:
	FScenario(FAutomationTestBase* InTest)
		: HoldAlarmFixture::FScenario(InTest, HoldAlarmFixture::ECase::Jev) {}

	virtual bool Update() override
	{
		double Now = 0.;
		const EStep Preparation = PrepareUpdate(Now);
		if (Preparation != EStep::Continue)
			return Preparation == EStep::Done;
		switch (Stage)
		{
		case EStage::Posts:
			return RunPosts(Now);
		case EStage::Respond:
			return RunRespond(Now);
		case EStage::Combat:
			return RunCombat(Now);
		case EStage::Return:
			return RunReturn(Now);
		default:
			return bFailed;
		}
	}
};
}

bool FHoldJevTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmJevTests::FScenario(this));
	return true;
}

#endif
