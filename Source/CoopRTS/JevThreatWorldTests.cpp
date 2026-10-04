#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevThreatWorldFixture.h"
#include "ObjectiveAnnouncer.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitBrainMapTest, "CoopRTS.Enemy.SplitBrain.Map",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitBrainPublishTest, "CoopRTS.Enemy.SplitBrain.Publish",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitBrainSoloTest, "CoopRTS.Enemy.SplitBrain.Solo",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitBrainFallbackTest, "CoopRTS.Enemy.SplitBrain.Fallback",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSplitBrainSkipTest, "CoopRTS.Enemy.SplitBrain.Skip",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevThreatKit;

// The pairs the map authors are the ones the rules accept: tagged on the region actors, distinct, non-adjacent necks.
class FMapScenario : public FThreatScenario
{
public:
	explicit FMapScenario(FAutomationTestBase* InTest) : FThreatScenario(InTest, false) {}

private:
	bool Prepare() override
	{
		const JevPlanner::FWorld World = PlannerWorld(*Kit.State);
		const TArray<JevThreat::FPair> Pairs = AuthoredPairs(*Kit.State);
		Check(Pairs.Num() >= 2, TEXT("The map authors at least two neck pairs, so a pair can be chosen and a fallback exists"));
		for (const JevThreat::FPair& Pair : Pairs)
		{
			Check(JevThreat::IsSupplyNeck(World, Pair.A) && JevThreat::IsSupplyNeck(World, Pair.B),
				*FString::Printf(TEXT("Pair %d,%d: both regions are human supply necks by the territory graph"), Pair.A, Pair.B));
			Check(Pair.A != Pair.B && !JevThreat::Adjacent(World, Pair.A, Pair.B),
				*FString::Printf(TEXT("Pair %d,%d: the regions are distinct and not adjacent"), Pair.A, Pair.B));
		}
		const JevThreat::FChoice Choice = JevThreat::ChoosePair(World, Pairs);
		Check(Choice.Skip == JevThreat::ESkip::None && Choice.bFallback,
			TEXT("With no region held, the rules still find an eligible pair on the real map, as a fallback"));
		return true;
	}

	bool Step() override { return true; }
};

// Two humans hold both regions of the first pair.
class FPublishScenario : public FFlowScenario
{
public:
	using FFlowScenario::FFlowScenario;

private:
	bool Place() override
	{
		Lancer = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Assault);
		Brawler = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Frontline);
		return Arrange({ 3, 8 });
	}

	bool OnPublished(bool bSeen) override
	{
		if (!Check(bSeen && Release().Cuts.Num() == 2, TEXT("Two humans: two Split-Brain Cut plans are published")))
			return true;
		Check(PublishedAt >= 330.f && PublishedAt < 334.f,
			*FString::Printf(TEXT("The plans appear 30 s before the release, from 330 s (seen at %.1f)"), PublishedAt));
		const FJevCutPlan* First = CutAt(3);
		const FJevCutPlan* Second = CutAt(8);
		if (!Check(First && Second, TEXT("They name the two regions of the first authored pair, both held by the humans")))
			return true;
		Check(First->Ticket > 0 && Second->Ticket > 0 && First->Ticket != Second->Ticket, TEXT("Each plan has its own ticket"));
		Check(First->Source == JevMain && Second->Source == JevMain, TEXT("Both march from JEV's main"));
		Check(First->SizeBand == JevPlanner::SizeBand(4) && Second->SizeBand == First->SizeBand,
			TEXT("Both publish the size band of four units"));
		for (const FJevCutPlan* Cut : { First, Second })
		{
			Check(Cut->EtaSeconds > JevThreat::LaunchTime() - PublishedAt + 10.f,
				*FString::Printf(TEXT("The ETA covers the wait for the launch plus the march (%.1f s)"), Cut->EtaSeconds));
			const AMapRegion* Region = RegionActor(*Kit.State, Cut->Target);
			Check(Region && Cut->Memo.Contains(JevThreat::Name) && Cut->Memo.Contains(Region->DisplayName.ToString())
					&& Cut->Memo.Contains(FString::Printf(TEXT("#%d"), Cut->Ticket)),
				TEXT("The memo names the threat, its ticket and its region"));
		}
		Check(EnemyForces(Kit.World).IsEmpty() && CutEvents() == 0 && Release().WaveCount == 2,
			TEXT("Nothing has launched: the plans are out ahead of their forces"));
		return Announced();
	}

	// One alert event, raised once when the plans publish, at the first target (the announcer pipeline voices it).
	bool Announced()
	{
		const TArray<const FObjectiveEvent*> Events = NewEvents(FName(JevThreat::AnnouncerId));
		Check(Events.Num() == 1, *FString::Printf(TEXT("The threat is announced exactly once (%d events)"), Events.Num()));
		if (!Events.IsEmpty())
			Check(Events[0]->RegionIndex == 3 && Events[0]->AffectedTeam == 0 && !Events[0]->RegionName.IsEmpty(),
				TEXT("The alert row points at the first target region and is the humans' team's"));
		return false;
	}

	bool BeforeLaunch() override
	{
		Check(Release().Cuts.Num() == 2 && EnemyForces(Kit.World).IsEmpty() && CutEvents() == 0,
			TEXT("Right up to 360 s the plans stand and no force exists"));
		return false;
	}

	bool OnLaunched() override
	{
		Check(Release().Cuts.IsEmpty(), TEXT("At launch the cut plans give way to the forces' own"));
		Check(Release().WaveCount == 3, TEXT("The release wave counts once; the cut forces are not release waves"));
		Check(CutEvents() == 2, TEXT("Both cut forces launch at the same release"));
		for (const int32 Region : { 3, 8 })
		{
			const FJevWaveEvent* Event = CutEvent(Region);
			Check(Event && Event->Release == 3 && Event->Budget == 260 && Event->Units == 4 && Event->Forces == 1,
				TEXT("Each force is funded with half of v2.0's 400 x 1.3: 260, and buys four units"));
			const TArray<AArmyGroup*> Forces = ForcesTargeting({ Region });
			if (!Check(Forces.Num() == 1, TEXT("One force attacks each region")))
				continue;
			const AArmyGroup* Force = Forces[0];
			const FJevPublishedPlan* Plan = PlanOf(Force);
			Check(CountUnits(Forces, Lancer) == 3 && CountUnits(Forces, Brawler) == 1 && ArmyTestSetup::CurrentRegion(Force) == JevMain,
				TEXT("It is three Lancers and a Brawler, spawned in JEV's main"));
			Check(Force->RetreatThreshold == ERetreatThreshold::Never && Force->SpeedFactor == 1.f,
				TEXT("It fights to the end at ordinary speed"));
			Check(Plan && Plan->Verb == EForceVerb::Attack && Plan->TargetRegionIndex == Region && Plan->TicketNumber > 0
					&& Plan->CommittedUntil > Kit.World->GetTimeSeconds() + Plan->EtaSeconds,
				TEXT("Its Attack plan is published and committed until it has arrived"));
		}
		Check(ForcesTargeting({ 3, 8 }).Num() == 2 && EnemyForces(Kit.World).Num() > 2,
			TEXT("The normal v2.0 wave launched on top of the two cut forces"));
		return true;
	}

	int32 Lancer = INDEX_NONE, Brawler = INDEX_NONE;
};

// Alone, JEV sends one force, to the region of the pair the human holds.
class FSoloScenario : public FFlowScenario
{
public:
	explicit FSoloScenario(FAutomationTestBase* InTest) : FFlowScenario(InTest, false) {}

private:
	bool Place() override { return Arrange({ 8 }); }

	bool OnPublished(bool bSeen) override
	{
		return !Check(bSeen && Release().Cuts.Num() == 1 && CutAt(8), TEXT("Solo: one plan, for the region the human holds"));
	}

	bool OnLaunched() override
	{
		const FJevWaveEvent* Event = CutEvent(8);
		Check(CutEvents() == 1 && Event && Event->Budget == 200 && Event->Units == 4,
			TEXT("Solo: one force funded with half of v2.0's 400: 200, four units"));
		Check(ForcesTargeting({ 8 }).Num() == 1 && ForcesTargeting({ 3 }).IsEmpty(), TEXT("Solo: only the held region is attacked"));
		return true;
	}
};

// With only West Cut held, the first pair that holds anything is the one JEV takes, whole.
class FFallbackScenario : public FFlowScenario
{
public:
	using FFlowScenario::FFlowScenario;

private:
	bool Place() override { return Arrange({ 2 }); }

	bool OnPublished(bool bSeen) override
	{
		Check(bSeen && Release().Cuts.Num() == 2 && CutAt(2) && CutAt(8) && !CutAt(3),
			TEXT("The humans hold one region of one pair: that pair is taken, its unheld region included"));
		return true;
	}

	bool OnLaunched() override { return true; }
};

// JEV holds a region of every authored pair: nothing is sent, and nothing is announced.
class FSkipScenario : public FFlowScenario
{
public:
	using FFlowScenario::FFlowScenario;

private:
	bool Place() override { return Arrange({ 3 }, { 8 }); }

	bool OnPublished(bool bSeen) override
	{
		Check(!bSeen && Release().Cuts.IsEmpty(), TEXT("No eligible pair: nothing is published"));
		Check(NewEvents(FName(JevThreat::AnnouncerId)).IsEmpty(), TEXT("and nothing is announced"));
		return false;
	}

	bool OnLaunched() override
	{
		Check(CutEvents() == 0 && ForcesTargeting({ 3 }).IsEmpty() && ForcesTargeting({ 8 }).IsEmpty(),
			TEXT("and no cut force launches; the release wave still does"));
		Check(Release().WaveCount == 3, TEXT("The v2.0 wave is unaffected by the skipped threat"));
		return true;
	}
};
}

bool FSplitBrainMapTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FMapScenario(this));
	return true;
}

bool FSplitBrainPublishTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPublishScenario(this, true));
	return true;
}

bool FSplitBrainSoloTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSoloScenario(this));
	return true;
}

bool FSplitBrainFallbackTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFallbackScenario(this, true));
	return true;
}

bool FSplitBrainSkipTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSkipScenario(this, true));
	return true;
}

#endif
