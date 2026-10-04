#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/JevExecution.h"
#include "Rules/JevReleasePolicy.h"

// JEV fields the Lancer and the Scrambler: its production roles, its wave purchases and its force-health
// estimate over the five catalogue roles (docs: Design/jev.md, Releases).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevNewUnitsRolesTest, "CoopRTS.Rules.JevNewUnits.Roles",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevNewUnitsWavesTest, "CoopRTS.Rules.JevNewUnits.Waves",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevNewUnitsHealthTest, "CoopRTS.Rules.JevNewUnits.Health",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace JevExecution;

JevRelease::FArmorCounts Humans(int32 Light, int32 Heavy, int32 Shielded)
{
	JevRelease::FArmorCounts Counts;
	Counts.Count[static_cast<int32>(EArmorClass::Light)] = Light;
	Counts.Count[static_cast<int32>(EArmorClass::Heavy)] = Heavy;
	Counts.Count[static_cast<int32>(EArmorClass::Shielded)] = Shielded;
	return Counts;
}

// The catalogue's five units in role order: Brawler, Rifle, Artillery, Lancer, Scrambler.
TArray<JevRelease::FUnitOption> Catalogue()
{
	return { { 20, EArmorClass::Heavy, EDamageType::Kinetic }, { 24, EArmorClass::Light, EDamageType::Piercing },
		{ 40, EArmorClass::Light, EDamageType::Demolition }, { 24, EArmorClass::Shielded, EDamageType::Piercing },
		{ 24, EArmorClass::Light, EDamageType::EMP } };
}
}

bool FJevNewUnitsRolesTest::RunTest(const FString&)
{
	const int32 None[RoleSlots] = {};
	const int32 Front[RoleSlots] = { 1, 0, 0, 0, 0 };
	TestEqual(TEXT("The first producer is Frontline even against a Shielded army"), NextRoleSlot(None, Humans(0, 0, 9)), 0);
	TestEqual(TEXT("Against a Shielded majority the next producer is Support, the EMP answer"),
		NextRoleSlot(Front, Humans(2, 3, 9)), SupportSlot);
	const int32 FrontSupport[RoleSlots] = { 1, 0, 0, 0, 1 };
	TestEqual(TEXT("With Support in place the base roles come back: Ranged next"), NextRoleSlot(FrontSupport, Humans(0, 0, 9)), 1);

	TestEqual(TEXT("Against a Heavy majority the next producer is Ranged"), NextRoleSlot(Front, Humans(2, 5, 1)), 1);
	const int32 FrontRanged[RoleSlots] = { 1, 1, 0, 0, 0 };
	TestEqual(TEXT("Then the Lancer, the other Piercing answer to Heavy, rather than Siege"),
		NextRoleSlot(FrontRanged, Humans(2, 5, 1)), AssaultSlot);
	const int32 FrontRangedLancer[RoleSlots] = { 1, 1, 0, 1, 0 };
	TestEqual(TEXT("Once both answers stand the unfilled base role follows"), NextRoleSlot(FrontRangedLancer, Humans(2, 5, 1)), 2);

	TestEqual(TEXT("Against a Light majority Frontline is the answer, so a filled one falls back to Ranged"),
		NextRoleSlot(Front, Humans(6, 2, 2)), 1);
	TestEqual(TEXT("Ties between classes go to the earlier class (Light)"), NextRoleSlot(FrontRanged, Humans(4, 4, 4)), 2);
	TestEqual(TEXT("The most numerous class decides, not the heaviest"), NextRoleSlot(Front, Humans(5, 0, 6)), SupportSlot);
	TestEqual(TEXT("With no humans in view the base rotation applies"), NextRoleSlot(Front, Humans(0, 0, 0)), 1);

	const int32 Full[RoleSlots] = { 1, 1, 1, 1, 1 };
	TestEqual(TEXT("With every role filled Frontline is next, as a tie always was"), NextRoleSlot(Full, Humans(0, 0, 9)), 0);
	const int32 RangedHeavy[RoleSlots] = { 1, 2, 1, 1, 1 };
	TestEqual(TEXT("Ranged outnumbering Frontline leaves Frontline next"), NextRoleSlot(RangedHeavy, Humans(0, 0, 9)), 0);
	const int32 FrontHeavy[RoleSlots] = { 3, 1, 1, 1, 1 };
	TestEqual(TEXT("Frontline outnumbering Ranged leaves Ranged next"), NextRoleSlot(FrontHeavy, Humans(0, 0, 9)), 1);
	return true;
}

bool FJevNewUnitsWavesTest::RunTest(const FString&)
{
	using namespace JevRelease;
	const TArray<FUnitOption> Options = Catalogue();
	FPurchase Wave = Purchase(150, Options, BehaviourFor(1), EArmorClass::Shielded);
	TestTrue(TEXT("v1.1 buys the cheapest unit even against Shielded: seven Brawlers"),
		Wave.Counts[0] == 7 && Wave.Units == 7 && Wave.Carry == 10);
	Wave = Purchase(250, Options, BehaviourFor(2), EArmorClass::Shielded);
	TestTrue(TEXT("v1.2 against Shielded buys ten Scramblers (EMP) and carries 10"),
		Wave.Counts[4] == 10 && Wave.Units == 10 && Wave.Carry == 10 && Wave.Counts[3] == 0);
	Wave = Purchase(260, Options, BehaviourFor(2), EArmorClass::Shielded);
	TestTrue(TEXT("The carry joins the pool: ten Scramblers, then a Brawler fills the rest"),
		Wave.Counts[4] == 10 && Wave.Counts[0] == 1 && Wave.Carry == 0);
	Wave = Purchase(250, Options, BehaviourFor(2), EArmorClass::Heavy);
	TestTrue(TEXT("Against Heavy the Rifle and the Lancer cost the same, so the earlier Rifle is bought"),
		Wave.Counts[1] == 10 && Wave.Counts[3] == 0);
	Wave = Purchase(300, Options, BehaviourFor(7), EArmorClass::Shielded);
	TestTrue(TEXT("An overrun wave counters Shielded too: twelve Scramblers"), Wave.Counts[4] == 12 && Wave.Carry == 12);
	Wave = Purchase(48, Options, BehaviourFor(2), EArmorClass::Shielded);
	TestTrue(TEXT("A pool of two Scramblers buys exactly them"), Wave.Counts[4] == 2 && Wave.Units == 2 && Wave.Carry == 0);
	return true;
}

bool FJevNewUnitsHealthTest::RunTest(const FString&)
{
	const FUnitHealth FullLancer{ 36, 36, 60, 60 };
	const FUnitHealth BareLancer{ 36, 36, 0, 60 };
	const FUnitHealth HurtBareLancer{ 18, 36, 0, 60 };
	const FUnitHealth HalfBrawler{ 165, 330, 0, 0 };
	TestEqual(TEXT("A full Lancer is at full health"), HealthFraction({ FullLancer }), 1.f);
	TestEqual(TEXT("A Lancer whose shield is gone is at 36 of 96 points, not at full HP"), HealthFraction({ BareLancer }), 36.f / 96.f);
	TestEqual(TEXT("A shieldless Lancer at half HP counts 18 of 96"), HealthFraction({ HurtBareLancer }), 18.f / 96.f);
	TestEqual(TEXT("An unshielded unit counts hit points alone"), HealthFraction({ HalfBrawler }), .5f);
	TestEqual(TEXT("A force is the mean of its units, each in its own points"),
		HealthFraction({ BareLancer, HalfBrawler }), (36.f / 96.f + .5f) / 2.f);
	TestEqual(TEXT("An empty force never reads as hurt"), HealthFraction({}), 1.f);
	TestEqual(TEXT("A unit with no points at all is skipped"), HealthFraction({ FUnitHealth{}, HalfBrawler }), .5f);
	TestTrue(TEXT("A hurt Lancer force is below the recovery line that its hit points alone would not reach"),
		HealthFraction({ HurtBareLancer }) < RecoveryEnterHealth && 18.f / 36.f >= RecoveryEnterHealth);
	return true;
}

#endif
