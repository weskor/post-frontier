#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevThreatWorldFixture.h"
#include "Rules/FortifyPolicy.h"

// The tuning target of Split-Brain Cut: a full Brawler squad (six, tier 1) holding a neck loses it to the cut force without
// Fortify and keeps it with Fortify. Both cases run the real threat from the real schedule against identical armies, on
// both regions of the authored pair (Skyhook and Reactor Yard); only the cast differs. A fight is chaotic: what an earlier
// scenario left in the world, or a start a frame off a whole world second, changes its numbers (docs: battle.md). Each runs
// in its own scope, so in a fresh process, and starts on a whole second (FThreatScenario), which makes it repeatable.
// The names do not start with CoopRTS.Enemy.SplitBrain, so that scope's filter does not pick them up.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitBrainUnfortifiedTest, "CoopRTS.Enemy.CutFight.Unfortified",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitBrainFortifiedTest, "CoopRTS.Enemy.CutFight.Fortified",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

DEFINE_LOG_CATEGORY_STATIC(LogSplitBrainFight, Log, All);

namespace
{
using namespace JevThreatKit;

// Server game seconds after the launch within which both regions must be settled.
constexpr double SettleBound = 200.;
// The cast goes out when the cut force is this close to the region's anchor: about ten seconds before it arrives, as a
// player reading the plan's ETA would cast, so the first shots already meet a Fortified squad.
constexpr float CastDistance = 4000.f;

struct FHeld
{
	int32 Region = INDEX_NONE;
	AArmyGroup* Defenders = nullptr;
	AArmyGroup* Cut = nullptr;
	ACommandPlayerState* Caster = nullptr;
	bool bCast = false;
	int32 DefenderHealth = 0;
	int32 CutHealth = 0;
	FString Result;
};

int32 Durability(const AArmyGroup* Force)
{
	int32 Total = 0;
	if (Force)
		for (const AArmyUnit* Unit : Force->GetUnits())
			Total += IsValid(Unit) && Unit->IsAlive() ? Unit->GetHealth() + Unit->GetShield() : 0;
	return Total;
}

class FFightScenario : public FFlowScenario
{
public:
	FFightScenario(FAutomationTestBase* InTest, bool bInFortify)
		: FFlowScenario(InTest, true), bFortify(bInFortify), PairRegions{ 3, 8 }
	{
	}

private:
	bool Place() override { return Arrange({ PairRegions[0], PairRegions[1] }); }

	// Identical armies: six Brawlers hold each region under Move & Hold, in the humans' hands.
	bool OnPublished(bool bSeen) override
	{
		if (!Check(bSeen, TEXT("The threat published")))
			return true;
		const int32 Brawler = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Frontline);
		Kit.Wallet->Data = Other->Data = FortifyPolicy::DataCost + 10;
		int32 Number = 7;
		for (int32 Slot = 0; Slot < 2; ++Slot)
		{
			const int32 Region = PairRegions[Slot];
			FHeld& Held = Regions.AddDefaulted_GetRef();
			Held.Region = Region;
			Held.Caster = Slot == 0 ? Kit.Wallet : Other;
			const TArray<int32> Squad = { Brawler, Brawler, Brawler, Brawler, Brawler, Brawler };
			Held.Defenders = AArmyGroup::SpawnFreeForce(*Kit.World, *Kit.Wallet, Kit.State->GetRegionAnchor(Region), Squad, Number++, 1.f);
			if (!Check(Held.Defenders != nullptr, TEXT("The holding squad spawned")))
				return true;
			FCommandService::SetRetreatThreshold(Kit.Wallet, Held.Defenders, ERetreatThreshold::Never);
			FCommandService::IssueForceOrder(Kit.Wallet, Held.Defenders, EForceVerb::MoveHold, Region);
			Held.DefenderHealth = Durability(Held.Defenders);
		}
		return false;
	}

	bool OnLaunched() override
	{
		// The v2.0 wave (which also raids a region next to the human main) and anything else JEV launched is not under test;
		// the cut forces are the newest JEV forces by force number, one per cut event.
		const TArray<AArmyGroup*> All = EnemyForces(Kit.World);
		TSet<const AArmyGroup*> Cuts;
		for (int32 Index = FMath::Max(0, All.Num() - CutEvents()); Index < All.Num(); ++Index)
			Cuts.Add(All[Index]);
		DestroyForces(Kit.World, [&Cuts](const AArmyGroup& Force) { return Force.GetTeamIndex() == 5 && !Cuts.Contains(&Force); });
		for (FHeld& Held : Regions)
		{
			const TArray<AArmyGroup*> Forces = ForcesTargeting({ Held.Region });
			if (!Check(Forces.Num() == 1, TEXT("One cut force attacks each held region")))
				return true;
			Held.Cut = Forces[0];
			Held.CutHealth = Durability(Held.Cut);
		}
		return false;
	}

	bool Afterwards() override
	{
		if (InStage() > SettleBound)
			return Fail(TEXT("The fight did not settle"));
		bool bSettled = true;
		for (FHeld& Held : Regions)
			bSettled &= Settle(Held);
		if (!bSettled)
			return false;
		for (const FHeld& Held : Regions)
		{
			UE_LOG(LogSplitBrainFight, Display,
				TEXT("SPLITBRAIN fortify=%d region=%d result=%s t=%.1f defenders=%d/%d cutForce=%d/%d"), bFortify, Held.Region, *Held.Result,
				InStage(), Durability(Held.Defenders), Held.DefenderHealth, Durability(Held.Cut), Held.CutHealth);
			Check(Held.Result == (bFortify ? TEXT("HELD") : TEXT("LOST")),
				*FString::Printf(TEXT("Region %d %s: its holding squad %s it (%s)"), Held.Region,
					bFortify ? TEXT("fortified") : TEXT("unfortified"), bFortify ? TEXT("keeps") : TEXT("loses"), *Held.Result));
		}
		return true;
	}

	// True once the region's fate is settled: the cut force died with the region still the humans' (HELD), or the
	// squad died and JEV took the region through the normal capture (LOST).
	bool Settle(FHeld& Held)
	{
		if (!Held.Result.IsEmpty())
			return true;
		AMapRegion* Region = RegionActor(*Kit.State, Held.Region);
		const int32 Controller = Kit.State->GetRegionController(Held.Region);
		const int32 CutAlive = Held.Cut->GetAliveCount();
		if (bFortify && !Held.bCast && CutAlive > 0
			&& FVector::Dist2D(Held.Cut->GetCenter(), Kit.State->GetRegionAnchor(Held.Region)) < CastDistance)
		{
			Held.bCast = true;
			const FCommandResult Cast = FCommandService::CastFortify(Held.Caster, Region);
			Check(Cast.IsAccepted() && Region->FortifyTeam == 0, *FString::Printf(TEXT("The cast on region %d is accepted: %s"), Held.Region, *Cast.Message));
		}
		if (CutAlive == 0)
			Held.Result = Controller == 0 ? TEXT("HELD") : TEXT("DEAD_BUT_LOST");
		else if (Held.Defenders->GetAliveCount() == 0 && Controller == 5)
			Held.Result = TEXT("LOST");
		return !Held.Result.IsEmpty();
	}

	bool bFortify;
	int32 PairRegions[2];
	TArray<FHeld> Regions;
};
}

bool FSplitBrainUnfortifiedTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFightScenario(this, false));
	return true;
}

bool FSplitBrainFortifiedTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFightScenario(this, true));
	return true;
}

#endif
