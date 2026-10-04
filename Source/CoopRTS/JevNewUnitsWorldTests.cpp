#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Content/MatchContent.h"
#include "EnemyCommander.h"
#include "JevProductionVerification.h"
#include "JevReleaseWorldFixture.h"

// JEV fields the Lancer and the Scrambler: its own production picks them by counter logic against the
// humans' visible composition, and its release waves buy them. The world is quarantined as in the release scenarios.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevNewUnitsScramblerTest, "CoopRTS.Enemy.NewUnits.Scrambler",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevNewUnitsLancerTest, "CoopRTS.Enemy.NewUnits.Lancer",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevNewUnitsWaveTest, "CoopRTS.Enemy.NewUnits.Wave",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

constexpr int32 NewUnitsLancerShield = 60;

// One squad of the humans: six of the same unit, standing near their HQ and never acting.
AArmyGroup* SpawnHumanSquad(const FKit& Kit, EUnitRole Role)
{
	TArray<int32> Roster;
	Roster.Init(ArmyTestSetup::UnitIndex(Kit.State, Role), 6);
	AArmyGroup* Humans = AArmyGroup::SpawnFreeForce(*Kit.World, *Kit.Wallet,
		ArmyTestSetup::FromFriendlyHQ(Kit.State, -250.f, 350.f, 100.f), Roster, 30, 1.f);
	if (Humans)
		Park(*Humans);
	return Humans;
}

// JEV's production choice with the real executor: barracks are added one at a time to an isolated planner
// that is evaluated by hand, and each takes the role the humans' composition calls for. The last barracks
// then produces a full squad, which is checked unit by unit.
class FNewUnitsProductionScenario : public FScenario
{
public:
	FNewUnitsProductionScenario(FAutomationTestBase* InTest, EUnitRole InHumans, TArray<EUnitRole> InRoles)
		: FScenario(InTest), HumanRole(InHumans), Roles(MoveTemp(InRoles))
	{
	}

private:
	bool Prepare() override
	{
		if (!SpawnHumanSquad(Kit, HumanRole))
			return Fail(TEXT("The human fixture squad could not spawn"));
		Kit.Planner->SetActorTickEnabled(false);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		return Stage == 1 ? ChooseRoles() : Stage == 2 ? FillSquad()
			: Stage == 3                               ? Healthy()
													   : Wounded();
	}

	ACommandBuilding* AddBarracks()
	{
		ACommandPlayerState* Jev = Kit.State->EnemyCommander;
		Jev->Resources = 5000;
		ACommandBuilding* Placed = JevProductionVerification::PlaceBarracks(*Kit.State);
		if (Placed)
			Placed->Tick(60.f);
		Jev->Resources = 0;
		return Placed;
	}

	bool ChooseRoles()
	{
		for (int32 At = 0; At < Roles.Num(); ++At)
		{
			const EUnitRole Expected = Roles[At];
			Barracks = AddBarracks();
			if (!Barracks || !Check(Barracks->IsComplete(), TEXT("A completed JEV barracks fixture stands in its main")))
				return Fail(TEXT("A JEV barracks fixture could not be placed"));
			Kit.Planner->EvaluatePlan();
			const UArmyUnitDefinition* Unit = Barracks->GetProductionDefinition();
			if (!Check(Barracks->bForceConfigured && Barracks->ProductionRole == Expected && Unit && Unit->Role == Expected,
					*FString::Printf(TEXT("Barracks %d takes the %d role the humans' composition calls for (got %d, configured %d)"),
						At + 1, static_cast<int32>(Expected), static_cast<int32>(Barracks->ProductionRole), Barracks->bForceConfigured)))
				return true;
			if (At + 1 < Roles.Num())
				Barracks->SetActorTickEnabled(false);
		}
		Kit.State->EnemyCommander->Resources = 500;
		Spent = Kit.State->EnemyCommander->Resources;
		const UArmyUnitDefinition* Definition = Barracks->GetProductionDefinition();
		for (int32 Pass = 0; Pass < 40; ++Pass)
			Barracks->TickProduction(Definition->UnitDuration + .3f);
		Enter(2);
		return false;
	}

	bool FillSquad()
	{
		AArmyGroup* Force = Barracks->ForceGroup;
		if (!IsValid(Force) || Force->GetPendingRecruitCount() > 0 || Force->GetJoinedCount() < 3)
			return !Check(InStage() < 40., TEXT("Supply delivery joins the produced squad within 40 game seconds"));
		const UArmyUnitDefinition* Definition = Barracks->GetProductionDefinition();
		const bool bLancer = Definition->Role == EUnitRole::Assault;
		int32 Members = 0;
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			if (!IsValid(Unit) || !Unit->IsAlive())
				continue;
			++Members;
			if (!Check(Unit->GetUnitRole() == Definition->Role && Unit->GetTeamIndex() == 5
						&& Unit->MaxShield() == (bLancer ? NewUnitsLancerShield : 0),
					TEXT("Every member is the catalogue unit of the locked role on JEV's team, a Lancer with its 60 shield")))
				return true;
		}
		const int32 Left = Kit.State->EnemyCommander->Resources;
		if (!Check(Members == 3 && Definition->Capacity == 3 && Left == Spent - 3 * Definition->UnitCost,
				*FString::Printf(TEXT("JEV's production tick fills one squad of 3 and charges three unit costs (members %d, wallet %d)"), Members, Left)))
			return true;
		if (!bLancer)
			return true;
		Barracks->SetActorTickEnabled(false);
		Kit.Planner->EvaluatePlan();
		Check(Force->Verb != EForceVerb::Retreat, TEXT("A Lancer force at full health and shield is not sent to recover"));
		Enter(3);
		return false;
	}

	// Past the 25 s commitment the planner decides afresh.
	bool Healthy()
	{
		if (InStage() < 26.)
			return false;
		Enter(4);
		return false;
	}

	// The planner counts shields: a Lancer force with its shield gone and half its hit points left is
	// at 18 of 96 points, below the 0.35 recovery line that hit points alone (0.5) would not reach.
	bool Wounded()
	{
		AArmyGroup* Force = Barracks->ForceGroup;
		for (AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
			{
				Unit->StripShield();
				Unit->ReceiveEnvironmentalDamage(Unit->MaxHealth() / 2);
			}
		Kit.Planner->EvaluatePlan();
		Check(Force->Verb == EForceVerb::Retreat,
			*FString::Printf(TEXT("A shieldless, half-health Lancer force recovers: the planner sends it back (verb %d)"), static_cast<int32>(Force->Verb)));
		return true;
	}

	EUnitRole HumanRole;
	TArray<EUnitRole> Roles;
	ACommandBuilding* Barracks = nullptr;
	int32 Spent = 0;
};

// A v1.2 wave against a Lancer-heavy army: v1.1 still buys the cheapest unit, v1.2 answers Shielded with Scramblers.
class FNewUnitsWaveScenario : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		if (!SpawnHumanSquad(Kit, EUnitRole::Assault))
			return Fail(TEXT("The Lancer fixture squad could not spawn"));
		Brawler = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Frontline);
		Lancer = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Assault);
		Scrambler = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Support);
		SkipTo(119.2f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		switch (Stage)
		{
		case 1:
			SkipTo(120.8f);
			Enter(2);
			return false;
		case 2:
			return WaveLaunched(1) ? FirstWave() : false;
		case 3:
			SkipTo(239.2f);
			Enter(4);
			return false;
		case 4:
			SkipTo(240.8f);
			Enter(5);
			return false;
		default:
			return WaveLaunched(2) ? SecondWave() : false;
		}
	}

	bool FirstWave()
	{
		const TArray<AArmyGroup*> Forces = EnemyForces(Kit.World);
		const FJevWaveEvent& Event = Release().Waves.Last();
		if (!Check(Event.Release == 1 && Event.Units == 7 && CountUnits(Forces, Brawler) == 7,
				TEXT("v1.1 buys the cheapest unit, seven Brawlers, though the humans field only Shielded units")))
			return true;
		Known = Forces;
		Enter(3);
		return false;
	}

	bool SecondWave()
	{
		const FJevWaveEvent& Event = Release().Waves.Last();
		TArray<AArmyGroup*> Fresh = EnemyForces(Kit.World);
		Fresh.RemoveAll([this](AArmyGroup* Force) { return Known.Contains(Force); });
		// 250 for one commander plus the 10 carried: ten Scramblers (EMP) and a Brawler for the rest.
		if (!Check(Event.Release == 2 && Event.Budget == 260 && Event.Units == 11 && Event.Forces == 2,
				*FString::Printf(TEXT("v1.2 buys 260 Power in 11 units and 2 forces (release %d budget %d units %d forces %d)"),
					Event.Release, Event.Budget, Event.Units, Event.Forces)))
			return true;
		Check(CountUnits(Fresh, Scrambler) == 10 && CountUnits(Fresh, Brawler) == 1 && CountUnits(Fresh, Lancer) == 0,
			*FString::Printf(TEXT("v1.2 counters the Lancer majority with Scramblers (Scramblers %d, Brawlers %d)"),
				CountUnits(Fresh, Scrambler), CountUnits(Fresh, Brawler)));
		return true;
	}

	int32 Brawler = INDEX_NONE, Lancer = INDEX_NONE, Scrambler = INDEX_NONE;
	TArray<AArmyGroup*> Known;
};
}

bool FJevNewUnitsScramblerTest::RunTest(const FString&)
{
	// Frontline first, then Support against a Shielded majority.
	ADD_LATENT_AUTOMATION_COMMAND(FNewUnitsProductionScenario(this, EUnitRole::Assault, { EUnitRole::Frontline, EUnitRole::Support }));
	return true;
}

bool FJevNewUnitsLancerTest::RunTest(const FString&)
{
	// Frontline, Ranged, then the Lancer against a Heavy majority.
	ADD_LATENT_AUTOMATION_COMMAND(
		FNewUnitsProductionScenario(this, EUnitRole::Frontline, { EUnitRole::Frontline, EUnitRole::Ranged, EUnitRole::Assault }));
	return true;
}

bool FJevNewUnitsWaveTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FNewUnitsWaveScenario(this));
	return true;
}

#endif
