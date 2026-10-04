#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "CombatTraitFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FShieldWorldTest, "CoopRTS.Combat.Shield.EmpStripAndRegen",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace CombatTraitFixture;

// Two shooters of equal damage, one EMP, each on its own shielded target. Real FireAt shots,
// real regen on the game clock.
class FShieldScenario : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	bool Setup() override;
	bool Step(double Now) override;

private:
	bool Volley(int32 Hit);

	TWeakObjectPtr<AArmyUnit> EmpShooter, KineticShooter, EmpTarget, KineticTarget;
	int32 ShieldAfterRestart = 0;
};

bool FShieldScenario::Setup()
{
	AMapRegion* Region = nullptr;
	AMapRegion* Other = nullptr;
	FVector Ground;
	if (!Check(Arena.PickRegions(Region, Other) && Arena.Ground(*Region, Ground), TEXT("The map offers a roomy navigable region for shield fixtures")))
		return false;
	FUnitSpec Shooter;
	Shooter.Damage = 20;
	Shooter.Range = 600.f;
	Shooter.Type = EDamageType::EMP;
	const int32 Emp = Arena.AddUnit(Shooter);
	Shooter.Type = EDamageType::Kinetic;
	const int32 Kinetic = Arena.AddUnit(Shooter);
	FUnitSpec Target;
	Target.Armor = EArmorClass::Shielded;
	Target.MaxHealth = 100;
	Target.MaxShield = 100;
	const int32 Shielded = Arena.AddUnit(Target);

	const FVector Base = Ground + FVector(0.f, 0.f, 65.f);
	EmpShooter = Arena.Spawn(false, Emp, Ground, Base + FVector(0.f, -200.f, 0.f));
	KineticShooter = Arena.Spawn(false, Kinetic, Ground, Base + FVector(0.f, 200.f, 0.f));
	EmpTarget = Arena.Spawn(true, Shielded, Ground, Base + FVector(300.f, -200.f, 0.f));
	KineticTarget = Arena.Spawn(true, Shielded, Ground, Base + FVector(300.f, 200.f, 0.f));
	return Check(EmpShooter.IsValid() && KineticShooter.IsValid() && EmpTarget.IsValid() && KineticTarget.IsValid(),
			   TEXT("Shield fixtures spawn as real units"))
		&& Check(EmpTarget->GetShield() == 100 && EmpTarget->MaxShield() == 100 && EmpTarget->GetHealth() == 100,
			TEXT("A shielded unit starts with a full shield above full HP"));
}

bool FShieldScenario::Volley(int32 Hit)
{
	Shoot(EmpShooter.Get(), EmpTarget.Get());
	Shoot(KineticShooter.Get(), KineticTarget.Get());
	// 20 damage: EMP spends 40 shield points a hit, Kinetic 20. The third EMP hit overflows by 20
	// points, which is 10 HP, with no class bonus.
	const int32 EmpShield[] = { 60, 20, 0 }, EmpHealth[] = { 100, 100, 90 }, KineticShield[] = { 80, 60, 40 };
	return Check(EmpTarget->GetShield() == EmpShield[Hit] && EmpTarget->GetHealth() == EmpHealth[Hit],
			   TEXT("EMP strips the shield at two points per damage, then the overflow damages HP without a bonus"))
		&& Check(KineticTarget->GetShield() == KineticShield[Hit] && KineticTarget->GetHealth() == 100,
			TEXT("An equal-damage Kinetic weapon strips one shield point per damage"));
}

bool FShieldScenario::Step(double Now)
{
	switch (Stage)
	{
	case 0:
		for (int32 Hit = 0; Hit < 3; ++Hit)
			if (!Volley(Hit))
				return true;
		Next(1, Now);
		return false;
	case 1:
		if (!After(Now, 3.))
			return false;
		if (!Check(KineticTarget->GetShield() == 40 && EmpTarget->GetShield() == 0,
				TEXT("No shield regenerates during the 4 s delay after damage")))
			return true;
		Next(2, Now);
		return false;
	case 2:
		if (!After(Now, 2.5))
			return false;
		// 5.5 s after the hits: regen began at 4 s and adds 10 points a second.
		if (!Check(KineticTarget->GetShield() > 40 && KineticTarget->GetShield() <= 60 && EmpTarget->GetShield() > 0 && EmpTarget->GetShield() <= 25,
				TEXT("Shield regenerates 10% of max per second once the delay has passed")))
			return true;
		Shoot(KineticShooter.Get(), KineticTarget.Get());
		ShieldAfterRestart = KineticTarget->GetShield();
		Next(3, Now);
		return false;
	case 3:
		if (!After(Now, 3.))
			return false;
		if (!Check(KineticTarget->GetShield() == ShieldAfterRestart, TEXT("Damage during regen restarts the 4 s delay")))
			return true;
		Next(4, Now);
		return false;
	default:
		if (!After(Now, 2.5))
			return false;
		Check(KineticTarget->GetShield() > ShieldAfterRestart, TEXT("Regen resumes after the restarted delay"));
		// The EMP target has regenerated for about 7 s since its last hit; its HP does not regenerate.
		Check(EmpTarget->GetShield() >= 55 && EmpTarget->GetShield() <= 85 && EmpTarget->GetHealth() == 90,
			TEXT("Only the shield regenerates; the EMP target keeps its lost HP"));
		return true;
	}
}
}

bool FShieldWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FShieldScenario(this));
	return true;
}

#endif
