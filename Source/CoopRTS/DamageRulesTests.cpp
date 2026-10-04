#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/DamagePolicy.h"
#include "Rules/RegionTraitPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDamageOrderTest, "CoopRTS.Rules.Damage.PipelineOrder",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDamageIncomingTest, "CoopRTS.Rules.Damage.IncomingMultipliers",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDamageShieldTest, "CoopRTS.Rules.Damage.ShieldAbsorption",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDamageEnvironmentalTest, "CoopRTS.Rules.Damage.Environmental",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FDamageOrderTest::RunTest(const FString& Parameters)
{
	DamagePolicy::FOutgoing Hit;
	Hit.Base = 40;
	Hit.Type = EDamageType::Demolition;
	Hit.Armor = EArmorClass::Structure;
	TestEqual(TEXT("A single-target hit takes the class bonus"), DamagePolicy::Outgoing(Hit), 60);
	Hit.SplashDistance = 100.f;
	TestEqual(TEXT("The class bonus precedes splash falloff"), DamagePolicy::Outgoing(Hit), 45);
	Hit.WorkshopMultiplier = DamagePolicy::SiegeOpticsOutgoingMultiplier;
	TestEqual(TEXT("Workshop follows falloff, truncating each step"), DamagePolicy::Outgoing(Hit), 33);
	Hit.SplashDistance = 200.01f;
	TestEqual(TEXT("Outside the splash radius nothing is left to multiply"), DamagePolicy::Outgoing(Hit), 0);

	DamagePolicy::FOutgoing Emp;
	Emp.Base = 20;
	Emp.Type = EDamageType::EMP;
	Emp.Armor = EArmorClass::Shielded;
	TestEqual(TEXT("EMP has no HP class bonus, even against Shielded"), DamagePolicy::Outgoing(Emp), 20);

	// Outgoing precedes incoming: 60 x 0.75 truncates once, then shield and HP see the result.
	const float Incoming[] = { DamagePolicy::EntrenchedIncomingMultiplier };
	const DamagePolicy::FResult Result = DamagePolicy::Resolve(DamagePolicy::Outgoing({ 40, EDamageType::Demolition, EArmorClass::Structure }),
		EDamageType::Demolition, 0, Incoming);
	TestEqual(TEXT("Class bonus then Entrenched"), Result.HealthLoss, 45);
	return true;
}

bool FDamageIncomingTest::RunTest(const FString& Parameters)
{
	TestEqual(TEXT("No multiplier leaves damage alone"), DamagePolicy::Incoming(37, {}), 37);
	const float Cover[] = { RegionTraitPolicy::CoverDamageMultiplier };
	TestEqual(TEXT("Cover takes 20% off"), DamagePolicy::Incoming(10, Cover), 8);
	TestEqual(TEXT("Fractional damage truncates"), DamagePolicy::Incoming(7, Cover), 5);
	const float Stacked[] = { .8f, .75f };
	TestEqual(TEXT("Multipliers multiply: Cover x 0.75 is 0.6"), DamagePolicy::Incoming(100, Stacked), 60);
	const float Reversed[] = { .75f, .8f };
	TestEqual(TEXT("Order of the list does not matter"), DamagePolicy::Incoming(37, Reversed), DamagePolicy::Incoming(37, Stacked));
	// Behaviour of today's rules: Entrenched was `Damage * 3 / 4` in integers.
	const float Entrenched[] = { DamagePolicy::EntrenchedIncomingMultiplier };
	for (int32 Damage = 0; Damage <= 400; ++Damage)
	{
		if (DamagePolicy::Incoming(Damage, Entrenched) != Damage * 3 / 4 || DamagePolicy::Incoming(Damage, Cover) != Damage * 4 / 5
			|| DamagePolicy::Incoming(Damage, Stacked) != Damage * 3 / 5)
		{
			AddError(FString::Printf(TEXT("Multiplier differs from exact integer arithmetic at damage %d"), Damage));
			break;
		}
	}
	return true;
}

bool FDamageShieldTest::RunTest(const FString& Parameters)
{
	using DamagePolicy::Absorb;
	TestEqual(TEXT("No shield: everything hits HP"), Absorb(20, EDamageType::Kinetic, 0).HealthLoss, 20);
	DamagePolicy::FResult Kinetic = Absorb(20, EDamageType::Kinetic, 50);
	TestTrue(TEXT("Shield larger than the hit absorbs all of it at one point per damage"), Kinetic.ShieldLoss == 20 && Kinetic.HealthLoss == 0);
	Kinetic = Absorb(20, EDamageType::Kinetic, 5);
	TestTrue(TEXT("Non-EMP overflow returns to HP one for one"), Kinetic.ShieldLoss == 5 && Kinetic.HealthLoss == 15);
	DamagePolicy::FResult Emp = Absorb(20, EDamageType::EMP, 50);
	TestTrue(TEXT("EMP spends two shield points per damage"), Emp.ShieldLoss == 40 && Emp.HealthLoss == 0);
	Emp = Absorb(20, EDamageType::EMP, 30);
	TestTrue(TEXT("EMP overflow converts back at the same ratio"), Emp.ShieldLoss == 30 && Emp.HealthLoss == 5);
	Emp = Absorb(21, EDamageType::EMP, 30);
	TestEqual(TEXT("EMP overflow truncates"), Emp.HealthLoss, 6);
	TestEqual(TEXT("EMP against no shield is plain damage"), Absorb(20, EDamageType::EMP, 0).HealthLoss, 20);
	TestFalse(TEXT("Zero damage takes nothing"), Absorb(0, EDamageType::EMP, 30).Any());

	// Equal DPS: EMP strips a 60-point shield in half the hits a Kinetic weapon needs.
	int32 EmpHits = 0, KineticHits = 0;
	for (int32 Shield = 60; Shield > 0; ++EmpHits)
		Shield -= Absorb(10, EDamageType::EMP, Shield).ShieldLoss;
	for (int32 Shield = 60; Shield > 0; ++KineticHits)
		Shield -= Absorb(10, EDamageType::Kinetic, Shield).ShieldLoss;
	TestTrue(TEXT("EMP strips shields in half the hits of an equal-damage weapon"), EmpHits == 3 && KineticHits == 6);

	// Incoming multipliers act before the shield sees the hit.
	const float Cover[] = { .8f };
	const DamagePolicy::FResult Covered = DamagePolicy::Resolve(10, EDamageType::Kinetic, 100, Cover);
	TestTrue(TEXT("Cover reduces the damage the shield absorbs"), Covered.ShieldLoss == 8 && Covered.HealthLoss == 0);
	const DamagePolicy::FResult CoveredEmp = DamagePolicy::Resolve(10, EDamageType::EMP, 100, Cover);
	TestEqual(TEXT("Cover applies before the EMP multiplier"), CoveredEmp.ShieldLoss, 16);
	return true;
}

bool FDamageEnvironmentalTest::RunTest(const FString& Parameters)
{
	DamagePolicy::FResult Result = DamagePolicy::Environmental(4, 10);
	TestTrue(TEXT("Environmental damage hits the shield first"), Result.ShieldLoss == 4 && Result.HealthLoss == 0);
	Result = DamagePolicy::Environmental(4, 3);
	TestTrue(TEXT("The excess reaches HP at one point per damage"), Result.ShieldLoss == 3 && Result.HealthLoss == 1);
	Result = DamagePolicy::Environmental(4, 0);
	TestTrue(TEXT("Without a shield it is plain HP damage"), Result.ShieldLoss == 0 && Result.HealthLoss == 4);
	TestFalse(TEXT("Non-positive environmental damage takes nothing"), DamagePolicy::Environmental(0, 10).Any());
	return true;
}

#endif
