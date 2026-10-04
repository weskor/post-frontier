#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "CombatTraitFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHighGroundWorldTest, "CoopRTS.Combat.Traits.HighGround",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FCoverWorldTest, "CoopRTS.Combat.Traits.Cover",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHazardWorldTest, "CoopRTS.Combat.Traits.Hazard",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace CombatTraitFixture;

// Region A carries the trait under test; region B is plain ground. Both are real map regions;
// units are teleported in and the cached region is re-read, as a refresh tick would after a move.
class FTraitScenario : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	bool Arrange(ERegionTrait TraitOfA)
	{
		if (!Check(Arena.PickRegions(A, B) && Arena.Ground(*A, GroundA) && Arena.Ground(*B, GroundB),
				TEXT("The map offers two roomy navigable regions for trait fixtures")))
			return false;
		Arena.SetTrait(*A, TraitOfA);
		return true;
	}
	static FVector At(const FVector& Ground, float X, float Y) { return Ground + FVector(X, Y, 65.f); }

	AMapRegion* A = nullptr;
	AMapRegion* B = nullptr;
	FVector GroundA = FVector::ZeroVector, GroundB = FVector::ZeroVector;
};

class FHighGroundScenario : public FTraitScenario
{
public:
	using FTraitScenario::FTraitScenario;

protected:
	bool Setup() override
	{
		if (!Arrange(ERegionTrait::HighGround))
			return false;
		FUnitSpec Gun;
		Gun.Damage = 10;
		Gun.Range = 500.f;
		FUnitSpec Dummy;
		Dummy.MaxHealth = 100;
		const int32 GunIndex = Arena.AddUnit(Gun), DummyIndex = Arena.AddUnit(Dummy);
		HighGun = Arena.Spawn(false, GunIndex, GroundA, At(GroundA, -280.f, 0.f));
		PlainGun = Arena.Spawn(false, GunIndex, GroundB, At(GroundB, -280.f, 0.f));
		HighTarget = Arena.Spawn(true, DummyIndex, GroundA, At(GroundA, 280.f, 0.f));
		PlainTarget = Arena.Spawn(true, DummyIndex, GroundB, At(GroundB, 280.f, 0.f));
		return Check(HighGun.IsValid() && PlainGun.IsValid() && HighTarget.IsValid() && PlainTarget.IsValid(),
			TEXT("High ground fixtures spawn as real units"));
	}

	bool Step(double Now) override
	{
		if (!Check(HighGun->GetRegionTrait() == ERegionTrait::HighGround && PlainGun->GetRegionTrait() == ERegionTrait::None,
				TEXT("Each unit caches the trait of the region it stands in")))
			return true;
		if (!Check(FMath::IsNearlyEqual(PlainGun->WeaponRange(), 500.f, .01f) && FMath::IsNearlyEqual(HighGun->WeaponRange(), 600.f, .01f),
				TEXT("High ground adds 20% weapon range to a unit inside it")))
			return true;
		// Targets 560 cm away: inside 600, outside 500.
		Shoot(HighGun.Get(), HighTarget.Get());
		Shoot(PlainGun.Get(), PlainTarget.Get());
		if (!Check(HighTarget->GetHealth() == 90 && PlainTarget->GetHealth() == 100,
				TEXT("A unit on high ground outranges an equal unit outside it")))
			return true;
		// The boundary sits at 600 cm.
		Arena.Place(HighTarget.Get(), HighGun->GetActorLocation() + FVector(610.f, 0.f, 0.f));
		Shoot(HighGun.Get(), HighTarget.Get());
		const bool bBeyond = HighTarget->GetHealth() == 90;
		Arena.Place(HighTarget.Get(), HighGun->GetActorLocation() + FVector(590.f, 0.f, 0.f));
		Shoot(HighGun.Get(), HighTarget.Get());
		Check(bBeyond && HighTarget->GetHealth() == 80, TEXT("The high-ground range ends at 600 cm"));
		// Leaving the region takes the bonus away.
		Arena.Place(HighGun.Get(), At(GroundB, -280.f, 0.f));
		Check(FMath::IsNearlyEqual(HighGun->WeaponRange(), 500.f, .01f), TEXT("A unit that leaves the region loses the range bonus"));
		return true;
	}

private:
	TWeakObjectPtr<AArmyUnit> HighGun, PlainGun, HighTarget, PlainTarget;
};

class FCoverScenario : public FTraitScenario
{
public:
	using FTraitScenario::FTraitScenario;

protected:
	bool Setup() override
	{
		if (!Arrange(ERegionTrait::Cover))
			return false;
		FUnitSpec Gun;
		Gun.Damage = 20;
		Gun.Range = 600.f;
		FUnitSpec Dummy;
		Dummy.MaxHealth = 200;
		const int32 GunIndex = Arena.AddUnit(Gun), DummyIndex = Arena.AddUnit(Dummy);
		CoverGun = Arena.Spawn(false, GunIndex, GroundA, At(GroundA, -280.f, 0.f));
		OpenGun = Arena.Spawn(false, GunIndex, GroundB, At(GroundB, -280.f, 0.f));
		CoverTarget = Arena.Spawn(true, DummyIndex, GroundA, At(GroundA, 0.f, 0.f));
		OpenTarget = Arena.Spawn(true, DummyIndex, GroundB, At(GroundB, 0.f, 0.f));
		return Check(CoverGun.IsValid() && OpenGun.IsValid() && CoverTarget.IsValid() && OpenTarget.IsValid(),
			TEXT("Cover fixtures spawn as real units"));
	}

	bool Step(double Now) override
	{
		Shoot(CoverGun.Get(), CoverTarget.Get());
		Shoot(OpenGun.Get(), OpenTarget.Get());
		if (!Check(CoverTarget->GetHealth() == 200 - 16 && OpenTarget->GetHealth() == 200 - 20,
				TEXT("Cover reduces damage taken by 20%")))
			return true;
		// The same unit takes full damage once the region stops being cover.
		A->Trait = ERegionTrait::None;
		Shoot(CoverGun.Get(), CoverTarget.Get());
		Check(CoverTarget->GetHealth() == 200 - 16 - 20, TEXT("Damage is full again when the region has no Cover trait"));
		return true;
	}

private:
	TWeakObjectPtr<AArmyUnit> CoverGun, OpenGun, CoverTarget, OpenTarget;
};

class FHazardScenario : public FTraitScenario
{
public:
	using FTraitScenario::FTraitScenario;

protected:
	bool Setup() override
	{
		if (!Arrange(ERegionTrait::Hazard))
			return false;
		FUnitSpec Steady;
		Steady.MaxHealth = 200;
		FUnitSpec Doomed;
		Doomed.MaxHealth = 20;
		FUnitSpec Shielded;
		Shielded.MaxHealth = 100;
		Shielded.MaxShield = 12;
		Shielded.Armor = EArmorClass::Shielded;
		const int32 SteadyIndex = Arena.AddUnit(Steady), DoomedIndex = Arena.AddUnit(Doomed), ShieldedIndex = Arena.AddUnit(Shielded);
		Idle = Arena.Spawn(false, SteadyIndex, GroundA, At(GroundA, 0.f, -200.f));
		Fragile = Arena.Spawn(false, DoomedIndex, GroundA, At(GroundA, 0.f, 200.f));
		Buffered = Arena.Spawn(false, ShieldedIndex, GroundA, At(GroundA, 0.f, 100.f));
		return Check(Idle.IsValid() && Fragile.IsValid() && Buffered.IsValid() && Idle->GetRegionTrait() == ERegionTrait::Hazard,
			TEXT("Hazard fixtures spawn as real units inside the Hazard region"));
	}

	bool Step(double Now) override
	{
		switch (Stage)
		{
		case 0:
			if (!After(Now, 2.5))
				return false;
			// Two or three ticks so far: the shield soaks them before any HP goes.
			if (!Check(Buffered->GetHealth() == 100 && Buffered->GetShield() < 12 && Idle->GetHealth() >= 188 && Idle->GetHealth() < 200,
					TEXT("Hazard damage hits the shield first, then HP")))
				return true;
			HealthBefore = Idle->GetHealth();
			Next(1, Now);
			return false;
		case 1:
			if (!After(Now, 4.))
				return false;
			if (!Check(!Fragile.IsValid() || !Fragile->IsAlive(), TEXT("Hazard kills a 20 HP idle unit within seconds")))
				return true;
			Next(2, Now);
			return false;
		case 2:
			if (!After(Now, 6.))
				return false;
			// Ten seconds since the first sample at 4 HP a second.
			if (!Check(HealthBefore - Idle->GetHealth() >= 36 && HealthBefore - Idle->GetHealth() <= 44,
					TEXT("Hazard deals 4 damage a second to an idle unit")))
				return true;
			if (!Check(Buffered->GetHealth() >= 60 && Buffered->GetHealth() <= 68 && Buffered->GetShield() == 0,
					TEXT("After the shield is gone the shielded unit loses HP at the same rate")))
				return true;
			HealthBefore = Idle->GetHealth();
			Arena.Place(Idle.Get(), At(GroundB, 0.f, 0.f));
			Next(3, Now);
			return false;
		default:
			if (!After(Now, 2.5))
				return false;
			Check(Idle->GetRegionTrait() == ERegionTrait::None && Idle->GetHealth() == HealthBefore,
				TEXT("A unit that leaves the Hazard region stops taking damage"));
			return true;
		}
	}

private:
	TWeakObjectPtr<AArmyUnit> Idle, Fragile, Buffered;
	int32 HealthBefore = 0;
};
}

bool FHighGroundWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHighGroundScenario(this));
	return true;
}

bool FCoverWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FCoverScenario(this));
	return true;
}

bool FHazardWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FHazardScenario(this));
	return true;
}

#endif
