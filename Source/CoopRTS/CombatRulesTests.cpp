#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/CombatPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCounterMatrixTest, "CoopRTS.Rules.Combat.CounterMatrix",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplashFalloffTest, "CoopRTS.Rules.Combat.SplashFalloff",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FCounterMatrixTest::RunTest(const FString& Parameters)
{
	const EArmorClass Armors[] = { EArmorClass::Light, EArmorClass::Heavy, EArmorClass::Shielded, EArmorClass::Structure };
	const EDamageType Types[] = { EDamageType::Kinetic, EDamageType::Piercing, EDamageType::Demolition, EDamageType::EMP };
	// Independent matrix: EMP's shield behavior is not an HP class bonus.
	const int32 Expected[][4] = {
		{ 60, 40, 40, 40 },
		{ 40, 60, 40, 40 },
		{ 40, 40, 40, 60 },
		{ 40, 40, 40, 40 }
	};
	for (int32 Type = 0; Type < 4; ++Type)
		for (int32 Armor = 0; Armor < 4; ++Armor)
		{
			const FString Label = FString::Printf(TEXT("Damage type %d against armor %d"), Type, Armor);
			TestEqual(Label, CombatPolicy::Damage(40, Types[Type], Armors[Armor]), Expected[Type][Armor]);
			TestEqual(Label + TEXT(" bonus classification"), CombatPolicy::IsStrongAgainst(Types[Type], Armors[Armor]),
				Expected[Type][Armor] == 60);
		}
	TestEqual(TEXT("Half HP from an odd boosted hit truncates"), CombatPolicy::Damage(15, EDamageType::Kinetic, EArmorClass::Light), 22);
	TestEqual(TEXT("Zero damage stays zero"), CombatPolicy::Damage(0, EDamageType::Demolition, EArmorClass::Structure), 0);
	return true;
}

bool FSplashFalloffTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("Impact gets full damage"), CombatPolicy::SplashDamage(55, 0.f), 55);
	TestEqual(TEXT("Half radius gets three-quarter damage, truncated"), CombatPolicy::SplashDamage(55, 100.f), 41);
	TestEqual(TEXT("Inclusive edge gets half damage, truncated"), CombatPolicy::SplashDamage(55, 200.f), 27);
	TestEqual(TEXT("Outside radius takes no damage"), CombatPolicy::SplashDamage(55, 200.01f), 0);
	TestEqual(TEXT("Zero damage stays zero"), CombatPolicy::SplashDamage(0, 100.f), 0);
	TestEqual(TEXT("Each victim's class bonus precedes falloff"),
		CombatPolicy::SplashDamage(CombatPolicy::Damage(40, EDamageType::Demolition, EArmorClass::Structure), 100.f), 45);
	return true;
}

#endif
