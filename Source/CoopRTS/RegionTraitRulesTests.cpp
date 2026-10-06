#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/DamagePolicy.h"
#include "Rules/RegionTraitPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionTraitEffectsTest, "CoopRTS.Rules.RegionTrait.Effects",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionTraitStackingTest, "CoopRTS.Rules.RegionTrait.Stacking",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRegionTraitHazardTest, "CoopRTS.Rules.RegionTrait.Hazard",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FRegionTraitEffectsTest::RunTest(const FString& Parameters)
{
	const ERegionTrait Traits[] = { ERegionTrait::None, ERegionTrait::HighGround, ERegionTrait::Cover, ERegionTrait::Open, ERegionTrait::Hazard };
	// Each trait has exactly one effect: range, damage taken, speed, or damage over time.
	const float Range[] = { 1.f, 1.2f, 1.f, 1.f, 1.f };
	const float Incoming[] = { 1.f, 1.f, .8f, 1.f, 1.f };
	const float Speed[] = { 1.f, 1.f, 1.f, 1.15f, 1.f };
	for (int32 Index = 0; Index < UE_ARRAY_COUNT(Traits); ++Index)
	{
		const FString Label = FString::Printf(TEXT("Trait %d"), Index);
		TestEqual(Label + TEXT(" range"), RegionTraitPolicy::RangeMultiplier(Traits[Index]), Range[Index]);
		TestEqual(Label + TEXT(" damage taken"), RegionTraitPolicy::IncomingMultiplier(Traits[Index]), Incoming[Index]);
		TestEqual(Label + TEXT(" speed"), RegionTraitPolicy::SpeedMultiplier(Traits[Index]), Speed[Index]);
	}
	return true;
}

bool FRegionTraitStackingTest::RunTest(const FString& Parameters)
{
	const float Cover = RegionTraitPolicy::IncomingMultiplier(ERegionTrait::Cover);
	const float Both[] = { Cover, DamagePolicy::EntrenchedIncomingMultiplier };
	TestEqual(TEXT("Cover 0.8 x Entrenched 0.75 takes 40% off"), DamagePolicy::Incoming(100, Both), 60);
	const float Cover3[] = { Cover, .75f, DamagePolicy::EntrenchedIncomingMultiplier };
	TestEqual(TEXT("A third multiplier (Fortify) composes the same way"), DamagePolicy::Incoming(100, Cover3), 45);
	const float OpenOnly[] = { RegionTraitPolicy::IncomingMultiplier(ERegionTrait::Open) };
	TestEqual(TEXT("Traits other than Cover leave incoming damage alone"), DamagePolicy::Incoming(100, OpenOnly), 100);
	return true;
}

bool FRegionTraitHazardTest::RunTest(const FString& Parameters)
{
	float Inside = 0.f;
	int32 Ticks = 0;
	for (int32 Step = 0; Step < 3; ++Step)
		Ticks += RegionTraitPolicy::AdvanceHazard(ERegionTrait::Hazard, Inside, .25f);
	TestEqual(TEXT("No tick before a full second inside"), Ticks, 0);
	Ticks += RegionTraitPolicy::AdvanceHazard(ERegionTrait::Hazard, Inside, .25f);
	TestEqual(TEXT("One tick after one second"), Ticks, 1);
	TestEqual(TEXT("A long step grants every whole tick and keeps the remainder"),
		RegionTraitPolicy::AdvanceHazard(ERegionTrait::Hazard, Inside, 2.5f), 2);
	TestEqual(TEXT("The remainder counts toward the next tick"),
		RegionTraitPolicy::AdvanceHazard(ERegionTrait::Hazard, Inside, .5f), 1);

	RegionTraitPolicy::AdvanceHazard(ERegionTrait::Hazard, Inside, .75f);
	TestEqual(TEXT("Leaving the region grants nothing"), RegionTraitPolicy::AdvanceHazard(ERegionTrait::None, Inside, 5.f), 0);
	TestEqual(TEXT("Re-entering waits a full second"), RegionTraitPolicy::AdvanceHazard(ERegionTrait::Hazard, Inside, .5f), 0);
	TestEqual(TEXT("Only Hazard ticks"), RegionTraitPolicy::AdvanceHazard(ERegionTrait::Cover, Inside, 10.f), 0);
	TestEqual(TEXT("Four damage per tick, 4 HP/s"), RegionTraitPolicy::HazardDamagePerTick, 4);
	return true;
}

#endif
