#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "CombatTraitFixture.h"
#include "Commands/CommandService.h"

// The shipped branch definitions (Build/Content/units.json) fighting as real units: the Warden has +30% HP, the
// Marksman outranges a Rifle, the Bulwark has +50% shield and -10% speed, the Jammer stuns a building for 5 s.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchStatsWorldTest, "CoopRTS.Forces.Branch.Content.Stats",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FBranchJammerWorldTest, "CoopRTS.Forces.Branch.Content.JammerStun",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace CombatTraitFixture;

int32 Find(const ACommandGameState& State, const TCHAR* Id)
{
	return State.Content->UnitIndexOf(FName(Id));
}

class FStatsScenario : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	bool Setup() override
	{
		AMapRegion* First = nullptr;
		AMapRegion* Second = nullptr;
		if (!Check(Arena.PickRegions(First, Second) && Arena.Ground(*First, Ground), TEXT("Branch fixtures find roomy ground")))
			return false;
		Arena.SetTrait(*First, ERegionTrait::None);
		Origin = Ground + FVector(-300.f, 0.f, 65.f);
		const ACommandGameState& State = *Arena.State;
		const UMatchContent& Content = *State.Content;
		Brawler = Find(State, TEXT("frontline"));
		Warden = Find(State, TEXT("warden"));
		Rifle = Find(State, TEXT("ranged"));
		Marksman = Find(State, TEXT("marksman"));
		Lancer = Find(State, TEXT("lancer"));
		Bulwark = Find(State, TEXT("bulwark"));
		return Check(Brawler >= 0 && Warden >= 0 && Rifle >= 0 && Marksman >= 0 && Lancer >= 0 && Bulwark >= 0
				&& Content.BranchIndexOf(Brawler) == Warden && Content.BranchIndexOf(Rifle) == Marksman && Content.BranchIndexOf(Lancer) == Bulwark
				&& Content.UnitIndexForRole(EUnitRole::Frontline) == Brawler && Content.UnitIndexForRole(EUnitRole::Ranged) == Rifle,
			TEXT("The catalogue pairs each base with its branch and role lookups still find the base"));
	}

	bool Step(double Now) override
	{
		const auto Spawn = [&](bool bHostile, int32 Index, float X) { return Arena.Spawn(bHostile, Index, Ground, Origin + FVector(X, 0.f, 0.f)); };
		AArmyUnit* Target = Spawn(true, Brawler, 800.f);
		AArmyUnit* Plain = Spawn(false, Brawler, 0.f);
		AArmyUnit* Guard = Spawn(false, Warden, 100.f);
		if (!Check(Plain && Guard && Plain->MaxHealth() == 330 && Guard->MaxHealth() == 429 && Guard->GetHealth() == 429,
				TEXT("A Warden spawns with 30% more HP than a Brawler (429 against 330) at full health")))
			return true;
		AArmyUnit* Rifleman = Spawn(false, Rifle, 200.f);
		AArmyUnit* Sharpshooter = Spawn(false, Marksman, 300.f);
		if (!Check(Rifleman && Sharpshooter && Target, TEXT("The range fixtures spawn")))
			return true;
		if (!Check(FMath::IsNearlyEqual(Sharpshooter->WeaponRange(), Rifleman->WeaponRange() * 1.2f, .5f) && Sharpshooter->WeaponRange() > Rifleman->WeaponRange(),
				*FString::Printf(TEXT("A Marksman's range is 20%% above a Rifle's (%.0f against %.0f)"), Sharpshooter->WeaponRange(), Rifleman->WeaponRange())))
			return true;
		// Both stand 600 cm from the target: inside a Marksman's 660, outside a Rifle's 550.
		Arena.Place(Rifleman, Target->GetActorLocation() + FVector(-600.f, 0.f, 0.f));
		Arena.Place(Sharpshooter, Target->GetActorLocation() + FVector(-600.f, 0.f, 0.f));
		const int32 Before = Target->GetHealth();
		Shoot(Rifleman, Target);
		const bool bRifleHit = Target->GetHealth() < Before;
		Shoot(Sharpshooter, Target);
		if (!Check(!bRifleHit && Target->GetHealth() < Before, TEXT("At 600 cm a Marksman hits and a Rifle cannot")))
			return true;
		AArmyUnit* Lance = Spawn(false, Lancer, 400.f);
		AArmyUnit* Shield = Spawn(false, Bulwark, 500.f);
		if (!Check(Lance && Shield && Lance->GetShield() == 60 && Shield->GetShield() == 90 && Shield->MaxShield() == 90,
				TEXT("A Bulwark's shield is 50% above a Lancer's (90 against 60)")))
			return true;
		Check(FMath::IsNearlyEqual(Shield->GetDefinition()->MoveSpeed, Lance->GetDefinition()->MoveSpeed * .9f, .01f)
				&& FMath::IsNearlyEqual(Shield->GetGroup()->GetBaseMarchSpeed(), Lance->GetDefinition()->MoveSpeed * .9f, .01f),
			TEXT("A Bulwark is 10% slower, and its force marches at that speed"));
		return true;
	}

	FVector Ground = FVector::ZeroVector, Origin = FVector::ZeroVector;
	int32 Brawler = INDEX_NONE, Warden = INDEX_NONE, Rifle = INDEX_NONE, Marksman = INDEX_NONE, Lancer = INDEX_NONE, Bulwark = INDEX_NONE;
};

// A hostile Jammer beside a Barracks: its first pulse stuns the building for 5 s, where the Scrambler's is 3 s.
class FJammerScenario : public FScenario
{
public:
	using FScenario::FScenario;

protected:
	bool Setup() override
	{
		AMapRegion* First = nullptr;
		AMapRegion* Second = nullptr;
		if (!Check(Arena.PickRegions(First, Second) && Arena.Ground(*First, Ground), TEXT("The Jammer fixture finds roomy ground")))
			return false;
		const ACommandGameState& State = *Arena.State;
		const UArmyUnitDefinition* Scrambler = State.Content->FindUnit(TEXT("scrambler"));
		const UArmyUnitDefinition* Jam = State.Content->FindUnit(TEXT("jammer"));
		if (!Check(Scrambler && Jam && Scrambler->PulseBuildingStunSeconds == 3.f && Jam->PulseBuildingStunSeconds == 5.f
					&& Jam->PulseInterval == Scrambler->PulseInterval && Jam->PulseRadius == Scrambler->PulseRadius,
				TEXT("The Jammer differs from the Scrambler only in its building stun: 5 s against 3 s")))
			return false;
		Arena.Wallet->Resources = 3000;
		if (!Check(PlaceBarracksNearHeadquarters(), TEXT("A Barracks is placed in friendly territory")))
			return false;
		Barracks->Tick(60.f);
		const FVector Beside = Barracks->GetActorLocation() + FVector(0.f, 330.f, 60.f);
		Jammer = Arena.Spawn(true, State.Content->IndexOf(Jam), Ground, Beside);
		return Check(Barracks->IsComplete() && Jammer.IsValid() && !Barracks->IsStunned(), TEXT("A finished Barracks and a Jammer beside it, not yet stunned"));
	}

	// The first legal Barracks site on a ring around the friendly HQ.
	bool PlaceBarracksNearHeadquarters()
	{
		const ACommandGameState& State = *Arena.State;
		const FVector Center = State.FriendlyHeadquarters->GetActorLocation();
		for (int32 Ring = 0; Ring < 9; ++Ring)
			for (int32 Direction = 0; Direction < 32; ++Direction)
			{
				const float Angle = Direction * PI / 16.f;
				FVector Point = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
				Point.Z = 5.f;
				Point = State.ResolveBuildingLocation(ArmyTestSetup::BarracksIndex, Point);
				FString Reason;
				if (State.FindRegionAt(Point) != State.FindRegionAt(Center) || !State.ValidateBuildingPlacement(ArmyTestSetup::BarracksIndex, 0, Point, Reason))
					continue;
				Barracks = FCommandService::PlaceBuilding(Arena.Wallet.Get(), ArmyTestSetup::BarracksIndex, Point).Building;
				if (Barracks.IsValid())
					return true;
			}
		return false;
	}

	bool Step(double Now) override
	{
		if (!Barracks->IsStunned())
			return !Check(!After(Now, 15.), TEXT("The Jammer's first pulse stuns the building"));
		const double Left = Barracks->StunEndServerTime - Arena.State->GetServerWorldTimeSeconds();
		Check(Left > 4.3 && Left <= 5.01, *FString::Printf(TEXT("The stun lasts 5 s (%.2f s left at the first sighting)"), Left));
		return true;
	}

	FVector Ground = FVector::ZeroVector;
	TWeakObjectPtr<ACommandBuilding> Barracks;
	TWeakObjectPtr<AArmyUnit> Jammer;
};
}

bool FBranchStatsWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FStatsScenario(this));
	return true;
}

bool FBranchJammerWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJammerScenario(this));
	return true;
}
#endif
