#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ForceCap.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FForceCapRulesTest, "CoopRTS.Rules.ForceCap",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FForceCapRulesTest::RunTest(const FString& Parameters)
{
	for (int32 Humans = 1; Humans <= 5; ++Humans)
	{
		const int32 Expected = Humans == 1 ? 5 : 4;
		TestEqual(TEXT("Solo has five slots; co-op has four"), ForceCap::Limit(Humans), Expected);
		TestTrue(TEXT("One free slot permits a human producer"), ForceCap::HasRoom(Expected - 1, ForceCap::Limit(Humans)));
		TestFalse(TEXT("The cap rejects the next human producer"), ForceCap::HasRoom(Expected, ForceCap::Limit(Humans)));
		TestFalse(TEXT("Joining co-op cannot add producers above the reduced cap"), ForceCap::HasRoom(Expected + 1, ForceCap::Limit(Humans)));
		TestTrue(TEXT("An uncapped commander has room"), ForceCap::HasRoom(Expected + 20, 0));
	}
	for (bool Alive : { false, true })
		for (bool Producer : { false, true })
			for (bool Owned : { false, true })
				TestEqual(TEXT("Only living owned production buildings occupy slots"),
					ForceCap::Counts(Alive, Producer, Owned), Alive && Producer && Owned);
	return true;
}
#endif
