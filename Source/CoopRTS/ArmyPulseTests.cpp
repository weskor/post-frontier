#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "CombatTraitFixture.h"
#include "Commands/CommandService.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPulseWorldTest, "CoopRTS.Combat.Pulse.StripAndStun",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace CombatTraitFixture;

constexpr float Radius = 400.f;

// A hostile Scrambler against a shieldless unit, a shielded unit and a producing Barracks, on the real
// unit tick and the real building tick: trigger set, effect, cooldown, stun, resume.
class FPulseScenario : public FScenario
{
public:
	using FScenario::FScenario;
	~FPulseScenario() override
	{
		if (SlowLancer.IsValid())
			SlowLancer->UnitDuration = LancerDuration;
	}

protected:
	bool Setup() override;
	bool Step(double Now) override;

private:
	bool PlaceBarracksNear(const FVector& Center, TWeakObjectPtr<ACommandBuilding>& Out);
	bool StandByHeadquarters();
	FVector Offset(float X) const { return Origin + FVector(X, 0.f, 0.f); }

	TWeakObjectPtr<AArmyUnit> Scrambler, Plain, Shielded, Fresh;
	TWeakObjectPtr<ACommandBuilding> Barracks, Foundation;
	FVector Ground = FVector::ZeroVector, Origin = FVector::ZeroVector;
	int32 ShieldedIndex = INDEX_NONE;
	TWeakObjectPtr<UArmyUnitDefinition> SlowLancer;
	float LancerDuration = 0.f;
	double FirstPulse = 0., StunStart = 0.;
	float FrozenProgress = 0.f, FrozenConstruction = 0.f;
};

bool FPulseScenario::PlaceBarracksNear(const FVector& Center, TWeakObjectPtr<ACommandBuilding>& Out)
{
	ACommandGameState* State = Arena.State.Get();
	Arena.Wallet->Resources = 3000;
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			Point.Z = 5.f;
			Point = State->ResolveBuildingLocation(ArmyTestSetup::BarracksIndex, Point);
			FString Reason;
			if (State->FindRegionAt(Point) != State->FindRegionAt(Center)
				|| !State->ValidateBuildingPlacement(ArmyTestSetup::BarracksIndex, 0, Point, Reason))
				continue;
			Out = FCommandService::PlaceBuilding(Arena.Wallet.Get(), ArmyTestSetup::BarracksIndex, Point).Building;
			if (Out.IsValid())
				return true;
		}
	return false;
}

bool FPulseScenario::StandByHeadquarters()
{
	// The far side of the HQ from the Barracks: the HQ is close, nothing else is within the radius.
	const FVector Hq = Arena.State->FriendlyHeadquarters->GetActorLocation();
	FVector Spot = Hq + (Hq - Barracks->GetActorLocation()).GetSafeNormal2D() * 250.f;
	Spot.Z = Scrambler->GetActorLocation().Z;
	const float Edge = FVector::Dist2D(Spot, Barracks->GetActorLocation())
		- ACommandBuilding::GetFootprintRadius(*Barracks->GetDefinition());
	if (!Check(Edge > Radius, TEXT("The HQ fixture spot is out of the Barracks' pulse range")))
		return false;
	Arena.Place(Scrambler.Get(), Spot);
	return true;
}

bool FPulseScenario::Setup()
{
	AMapRegion* First = nullptr;
	AMapRegion* Second = nullptr;
	if (!Check(Arena.PickRegions(First, Second)
				&& PlaceBarracksNear(Arena.State->FriendlyHeadquarters->GetActorLocation(), Barracks),
			TEXT("Pulse fixtures find roomy ground and a Barracks site")))
		return false;
	// The Barracks builds with the real tick, then produces the catalogue Lancer made slow (restored on exit)
	// so progress is continuous.
	Barracks->Tick(60.f);
	SlowLancer = const_cast<UArmyUnitDefinition*>(Arena.State->Content->Unit(ArmyTestSetup::UnitIndex(Arena.State.Get(), EUnitRole::Assault)));
	if (!Check(SlowLancer.IsValid(), TEXT("The catalogue has a Lancer to slow down")))
		return false;
	LancerDuration = SlowLancer->UnitDuration;
	SlowLancer->UnitDuration = 60.f;
	FCommandService::ConfigureProduction(Arena.Wallet.Get(), Barracks.Get(), EUnitRole::Assault, true);
	if (!Check(Barracks->IsComplete() && Barracks->bProductionEnabled && Barracks->GetProductionDuration() == 60.f,
			TEXT("A completed Barracks starts producing the slow fixture unit")))
		return false;
	// Fixture ground well away from the Barracks, so only the scripted moves bring it into range.
	AMapRegion* Far = FVector::Dist2D(Arena.State->GetRegionAnchor(First->RegionIndex), Barracks->GetActorLocation())
			> FVector::Dist2D(Arena.State->GetRegionAnchor(Second->RegionIndex), Barracks->GetActorLocation())
		? First
		: Second;
	if (!Check(Arena.Ground(*Far, Ground)
				&& FVector::Dist2D(Ground, Barracks->GetActorLocation()) > 2000.f,
			TEXT("Pulse fixtures stand more than 2000 cm from the Barracks")))
		return false;
	Origin = Ground + FVector(-300.f, 0.f, 65.f);

	FUnitSpec Scram;
	Scram.Role = EUnitRole::Support;
	Scram.Armor = EArmorClass::Light;
	Scram.Type = EDamageType::EMP;
	Scram.MaxHealth = 100000;
	Scram.Damage = 12;
	Scram.Range = 550.f;
	Scram.PulseInterval = 10.f;
	Scram.PulseRadius = Radius;
	Scram.PulseStun = 3.f;
	FUnitSpec Unshielded;
	FUnitSpec Lancer;
	Lancer.Armor = EArmorClass::Shielded;
	Lancer.MaxHealth = 110;
	Lancer.MaxShield = 80;
	const int32 ScramIndex = Arena.AddUnit(Scram), PlainIndex = Arena.AddUnit(Unshielded);
	ShieldedIndex = Arena.AddUnit(Lancer);
	Scrambler = Arena.Spawn(true, ScramIndex, Ground, Offset(0.f));
	Plain = Arena.Spawn(false, PlainIndex, Ground, Offset(100.f));
	// Shielded but just outside the pulse radius.
	Shielded = Arena.Spawn(false, ShieldedIndex, Ground, Offset(Radius + 50.f));
	return Check(Scrambler.IsValid() && Plain.IsValid() && Shielded.IsValid() && Shielded->GetShield() == 80,
		TEXT("Pulse fixtures spawn as real units"));
}

bool FPulseScenario::Step(double Now)
{
	switch (Stage)
	{
	case 0:
		// A shieldless unit in range and a shielded unit out of range are not in the trigger set;
		// the Barracks is far away. Wait for production to be under way too.
		if (!After(Now, 1.) || Barracks->ProductionProgressSeconds < .5f)
			return false;
		if (!Check(Shielded->GetShield() == 80 && Shielded->MaxShield() == 80 && Scrambler->GetLastPulseServerTime() < 0. && !Barracks->IsStunned(),
				TEXT("No pulse while only a shieldless unit is in range and the shielded unit is outside it")))
			return true;
		if (!StandByHeadquarters())
			return true;
		Next(7, Now);
		return false;
	case 7:
		if (!After(Now, 1.))
			return false;
		// A hostile HQ is never a trigger, however close.
		if (!Check(Scrambler->GetLastPulseServerTime() < 0. && Shielded->GetShield() == 80 && !Barracks->IsStunned(),
				TEXT("A hostile HQ alone within the radius does not make the Scrambler pulse")))
			return true;
		// Trigger present, but the Scrambler's force is retreating.
		Arena.Place(Scrambler.Get(), Offset(0.f));
		Arena.Place(Shielded.Get(), Offset(150.f));
		Scrambler->GetGroup()->Status = EForceStatus::Retreating;
		Next(8, Now);
		return false;
	case 8:
		if (!After(Now, 1.))
			return false;
		if (!Check(Scrambler->GetLastPulseServerTime() < 0. && Shielded->GetShield() == 80,
				TEXT("A Scrambler whose force is retreating does not pulse")))
			return true;
		Scrambler->GetGroup()->Status = EForceStatus::Holding;
		Next(1, Now);
		return false;
	case 1:
		if (Shielded->GetShield() > 0)
			return !Check(!After(Now, 1.5), TEXT("A shielded hostile in range makes the Scrambler pulse"));
		FirstPulse = Scrambler->GetLastPulseServerTime();
		if (!Check(FirstPulse > 0. && FirstPulse <= Now && Now - FirstPulse < 1.5, TEXT("The Scrambler publishes the server time of the cast for clients to draw")))
			return true;
		if (!Check(Shielded->GetHealth() == 110 && Plain->GetHealth() == 100 && !Barracks->IsStunned(),
				TEXT("The pulse empties the shield without HP damage and stuns nothing out of range")))
			return true;
		// A second shielded unit arrives while the 10 s cooldown runs.
		Fresh = Arena.Spawn(false, ShieldedIndex, Ground, Offset(200.f));
		if (!Check(Fresh.IsValid(), TEXT("A second shielded unit spawns")))
			return true;
		Next(2, Now);
		return false;
	case 2:
		if (!After(Now, 3.))
			return false;
		if (!Check(Fresh->GetShield() == 80, TEXT("The pulse is on its 10 s cooldown, so a new target keeps its shield"))
			|| !Check(Shielded->GetShield() == 0, TEXT("The pulse restarted the stripped unit's 4 s regen delay")))
			return true;
		// Move the Scrambler between the Barracks and a new foundation; buildings in range trigger it once the cooldown is over.
		if (!Check(PlaceBarracksNear(Barracks->GetActorLocation(), Foundation), TEXT("A foundation site exists next to the Barracks")))
			return true;
		Arena.Place(Scrambler.Get(), (Barracks->GetActorLocation() + Foundation->GetActorLocation()) * .5f + FVector(0.f, 0.f, 65.f));
		Next(3, Now);
		return false;
	case 3:
		if (!Barracks->IsStunned())
			return !Check(Now - FirstPulse < 11.5, TEXT("A building in range triggers the next pulse when the cooldown ends"));
		if (!Check(Scrambler->GetLastPulseServerTime() - FirstPulse >= 10. - 1e-3
					&& FMath::IsNearlyEqual(Barracks->StunEndServerTime, Scrambler->GetLastPulseServerTime() + 3., .01),
				TEXT("The next pulse comes 10 s after the first cast, and the replicated stun end is 3 s after it")))
			return true;
		StunStart = Now;
		FrozenProgress = Barracks->ProductionProgressSeconds;
		FrozenConstruction = Foundation->ConstructionProgress;
		if (!Check(Foundation->IsStunned() && !Foundation->IsComplete() && FrozenConstruction > 0.f,
				TEXT("The same pulse stunned a building that is still under construction")))
			return true;
		Next(4, Now);
		return false;
	case 4:
		if (!After(Now, 2.))
			return false;
		if (!Check(Barracks->IsStunned() && Barracks->ProductionProgressSeconds == FrozenProgress,
				TEXT("A stunned Barracks keeps its production progress frozen")))
			return true;
		if (!Check(Foundation->IsStunned() && Foundation->ConstructionProgress == FrozenConstruction,
				TEXT("A stunned building under construction keeps its construction progress frozen")))
			return true;
		Next(5, Now);
		return false;
	case 5:
		if (!After(Now, 1.5))
			return false;
		if (!Check(!Barracks->IsStunned(), TEXT("The stun ends 3 s after the pulse")))
			return true;
		Next(6, Now);
		return false;
	default:
		if (!After(Now, 1.))
			return false;
		Check(Barracks->ProductionProgressSeconds > FrozenProgress && Foundation->ConstructionProgress > FrozenConstruction,
			TEXT("Production and construction resume when the stun ends"));
		return true;
	}
}
}

bool FPulseWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPulseScenario(this));
	return true;
}

#endif
