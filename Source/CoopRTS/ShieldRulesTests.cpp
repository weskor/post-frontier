#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ShieldPolicy.h"

// Pure rule tests: no world, no actors. Values are arbitrary; assertions are invariants.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShieldRegenTest, "CoopRTS.Rules.Shield.Regen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShieldPulseTest, "CoopRTS.Rules.Shield.Pulse",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShieldStunTest, "CoopRTS.Rules.Shield.Stun",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
// Quarter-second steps are exact in binary, so the delay boundary is not float noise.
int32 Run(ShieldPolicy::FShieldClock& Clock, int32& Shield, int32 Max, float Seconds)
{
	int32 Gained = 0;
	for (float Elapsed = 0.f; Elapsed < Seconds; Elapsed += .25f)
	{
		const int32 Gain = ShieldPolicy::Regenerate(Clock, Shield, Max, .25f);
		Shield += Gain;
		Gained += Gain;
	}
	return Gained;
}
}

bool FShieldRegenTest::RunTest(const FString& Parameters)
{
	ShieldPolicy::FShieldClock Clock;
	int32 Shield = 0;
	TestEqual(TEXT("Nothing regenerates during the 4 s delay"), Run(Clock, Shield, 100, ShieldPolicy::RegenDelaySeconds), 0);
	TestEqual(TEXT("Then 10% of max per second"), Run(Clock, Shield, 100, 1.f), 10);
	TestEqual(TEXT("And keeps going at that rate"), Run(Clock, Shield, 100, 2.f), 20);

	// A hit restarts the delay and drops the carry.
	ShieldPolicy::RestartRegen(Clock);
	TestEqual(TEXT("Damage restarts the delay"), Run(Clock, Shield, 100, ShieldPolicy::RegenDelaySeconds), 0);
	TestEqual(TEXT("Regen resumes after the restarted delay"), Run(Clock, Shield, 100, 1.f), 10);

	// Max 25 regenerates 2.5 per second: fractions carry, so nothing is lost or invented.
	ShieldPolicy::FShieldClock Small;
	int32 SmallShield = 0;
	Run(Small, SmallShield, 25, ShieldPolicy::RegenDelaySeconds);
	TestEqual(TEXT("2.5 points per second grants whole points only"), Run(Small, SmallShield, 25, 1.f), 2);
	TestEqual(TEXT("The carried half point completes with the next second"), Run(Small, SmallShield, 25, 1.f), 3);
	TestEqual(TEXT("Two seconds of regen grant exactly 5 points"), SmallShield, 5);

	// Capped at max, and a full shield holds no stale carry.
	ShieldPolicy::FShieldClock Capped;
	int32 Nearly = 95;
	Run(Capped, Nearly, 100, 20.f);
	TestEqual(TEXT("Regen never exceeds the maximum"), Nearly, 100);
	TestEqual(TEXT("A full shield keeps no carry"), Capped.Carry, 0.f);

	// One long step crossing the delay only counts the time after it.
	ShieldPolicy::FShieldClock Long;
	TestEqual(TEXT("A 5 s step regenerates only its last second"), ShieldPolicy::Regenerate(Long, 0, 100, 5.f), 10);
	TestEqual(TEXT("A unit without a shield never regenerates"), ShieldPolicy::Regenerate(Long, 0, 0, 10.f), 0);
	return true;
}

bool FShieldPulseTest::RunTest(const FString& Parameters)
{
	using ShieldPolicy::EPulseSubject;
	TestTrue(TEXT("The first pulse is ready at spawn"), ShieldPolicy::PulseReady(0., 0.));
	const double Ready = ShieldPolicy::NextPulseReadyAt(12., 10.f);
	TestEqual(TEXT("The cooldown starts at the cast"), Ready, 22.);
	TestFalse(TEXT("Not ready before the cooldown ends"), ShieldPolicy::PulseReady(21.9, Ready));
	TestTrue(TEXT("Ready exactly when it ends"), ShieldPolicy::PulseReady(22., Ready));

	TestTrue(TEXT("A hostile unit with shield in range triggers"), ShieldPolicy::PulseTriggers(EPulseSubject::Unit, 1, 399.f, 400.f));
	TestTrue(TEXT("The radius is inclusive"), ShieldPolicy::PulseTriggers(EPulseSubject::Unit, 50, 400.f, 400.f));
	TestFalse(TEXT("A shieldless unit does not trigger"), ShieldPolicy::PulseTriggers(EPulseSubject::Unit, 0, 100.f, 400.f));
	TestFalse(TEXT("A unit out of range does not trigger"), ShieldPolicy::PulseTriggers(EPulseSubject::Unit, 50, 400.1f, 400.f));
	TestTrue(TEXT("A hostile building in range triggers whatever its state"), ShieldPolicy::PulseTriggers(EPulseSubject::Building, 0, 250.f, 400.f));
	TestFalse(TEXT("A building out of range does not trigger"), ShieldPolicy::PulseTriggers(EPulseSubject::Building, 0, 400.1f, 400.f));
	TestTrue(TEXT("A shieldless unit in range is still affected"), ShieldPolicy::PulseAffects(100.f, 400.f));
	TestFalse(TEXT("Nothing outside the radius is affected"), ShieldPolicy::PulseAffects(400.1f, 400.f));
	return true;
}

bool FShieldStunTest::RunTest(const FString& Parameters)
{
	TestFalse(TEXT("A building that was never stunned is not stunned"), ShieldPolicy::IsStunned(0., -1.));
	const double End = ShieldPolicy::StunEndTime(10., -1., 3.f);
	TestEqual(TEXT("A stun lasts the given seconds"), End, 13.);
	TestTrue(TEXT("Stunned just before the end"), ShieldPolicy::IsStunned(12.9, End));
	TestFalse(TEXT("Free exactly at the end"), ShieldPolicy::IsStunned(13., End));
	TestEqual(TEXT("A re-stun refreshes to the full duration"), ShieldPolicy::StunEndTime(12., End, 3.f), 15.);
	TestEqual(TEXT("Stuns never stack"), ShieldPolicy::StunEndTime(10., End, 3.f), 13.);
	TestEqual(TEXT("A shorter stun never shortens a longer one"), ShieldPolicy::StunEndTime(11., 15., 3.f), 15.);
	return true;
}

#endif
