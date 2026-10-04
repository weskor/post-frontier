#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/TargetingPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTargetingOrderTest, "CoopRTS.Rules.Combat.TargetingOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FTargetingOrderTest::RunTest(const FString& Parameters)
{
	const EDamageType Types[] = { EDamageType::Kinetic, EDamageType::Piercing, EDamageType::Demolition };
	const EArmorClass Preferred[] = { EArmorClass::Light, EArmorClass::Heavy, EArmorClass::Structure };
	const EArmorClass Other[] = { EArmorClass::Heavy, EArmorClass::Light, EArmorClass::Light };
	for (int32 Type = 0; Type < 3; ++Type)
	{
		FTargetSelection Selection;
		TestEqual(TEXT("No eligible enemies means no target"), Selection.Index, INDEX_NONE);
		Selection.Consider(Types[Type], 0, Other[Type], 1.);
		Selection.Consider(Types[Type], 1, Preferred[Type], 100.);
		TestEqual(TEXT("Counter armor outranks a nearer non-counter"), Selection.Index, 1);
		Selection.Consider(Types[Type], 2, Other[Type], .5);
		TestEqual(TEXT("Later nearer non-counter cannot displace a counter"), Selection.Index, 1);
		Selection.Consider(Types[Type], 3, Preferred[Type], 25.);
		TestEqual(TEXT("Nearest matching armor wins"), Selection.Index, 3);
		Selection.Consider(Types[Type], 4, Preferred[Type], 25.);
		TestEqual(TEXT("Exact tie retains the first candidate"), Selection.Index, 3);

		FTargetSelection Fallback;
		Fallback.Consider(Types[Type], 0, EArmorClass::Shielded, 100.);
		Fallback.Consider(Types[Type], 1, Other[Type], 4.);
		TestEqual(TEXT("Without a counter target nearest wins across classes"), Fallback.Index, 1);
	}
	FTargetSelection EMP;
	EMP.Consider(EDamageType::EMP, 0, EArmorClass::Shielded, 100.);
	EMP.Consider(EDamageType::EMP, 1, EArmorClass::Heavy, 25.);
	TestEqual(TEXT("EMP prefers a Shielded target over a nearer non-Shielded one"), EMP.Index, 0);
	FTargetSelection EMPFallback;
	EMPFallback.Consider(EDamageType::EMP, 0, EArmorClass::Light, 100.);
	EMPFallback.Consider(EDamageType::EMP, 1, EArmorClass::Heavy, 25.);
	TestEqual(TEXT("EMP has no HP preference among other classes: nearest wins"), EMPFallback.Index, 1);
	return true;
}

#endif
