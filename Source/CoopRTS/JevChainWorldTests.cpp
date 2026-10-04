#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "CapturePoint.h"
#include "DepositSite.h"
#include "JevReleaseWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevChainDrillRigsTest, "CoopRTS.Enemy.Chain.DrillRigs",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

constexpr int32 EvaluationPasses = 8;

// A free deposit in a region two hops from JEV's main through a non-main region, so that region
// can be cut off by changing one controller. Index fields are INDEX_NONE when the map has none.
struct FChainSite
{
	int32 Cut = INDEX_NONE;
	int32 Island = INDEX_NONE;
};

AMapRegion* FindRegion(const ACommandGameState& State, int32 Index)
{
	for (AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex == Index)
			return Region;
	return nullptr;
}

FChainSite FindSite(const ACommandGameState& State, int32 Home)
{
	int32 Parent[ForceOrders::MaxRegions], Distance[ForceOrders::MaxRegions];
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
		Parent[Index] = Distance[Index] = INDEX_NONE;
	TArray<int32> Queue = { Home };
	Distance[Home] = 0;
	for (int32 Read = 0; Read < Queue.Num(); ++Read)
		for (const AMapRegion* Region : State.Regions)
			if (IsValid(Region) && Region->RegionIndex < ForceOrders::MaxRegions
				&& Distance[Region->RegionIndex] == INDEX_NONE && Region->Neighbours.Contains(Queue[Read]))
			{
				Distance[Region->RegionIndex] = Distance[Queue[Read]] + 1;
				Parent[Region->RegionIndex] = Queue[Read];
				Queue.Add(Region->RegionIndex);
			}
	FChainSite Site;
	for (const ADepositSite* Deposit : State.Deposits)
	{
		const int32 Region = IsValid(Deposit) && !IsValid(Deposit->Extractor) ? Deposit->RegionIndex : INDEX_NONE;
		if (Region < 0 || Region >= ForceOrders::MaxRegions || Distance[Region] != 2
			|| (Site.Island != INDEX_NONE && Region > Site.Island))
			continue;
		const AMapRegion* Cut = FindRegion(State, Parent[Region]);
		if (Cut && Cut->RegionRole != ERegionRole::Main)
		{
			Site.Island = Region;
			Site.Cut = Cut->RegionIndex;
		}
	}
	return Site;
}

class FChainScenario : public IAutomationLatentCommand
{
public:
	explicit FChainScenario(FAutomationTestBase* InTest) : Test(InTest) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 90.)
		{
			Test->AddError(TEXT("Chain scenario exceeded its real-time bound"));
			return true;
		}
		if (!Acquire(Kit))
			return false;
		Run();
		return true;
	}

private:
	int32 JevRigs() const
	{
		int32 Rigs = 0;
		for (const ADepositSite* Deposit : Kit.State->Deposits)
			Rigs += IsValid(Deposit) && IsValid(Deposit->Extractor) && Deposit->Extractor->OwningPlayerState == Kit.State->EnemyCommander;
		return Rigs;
	}
	bool IslandBuilt(int32 Island) const
	{
		for (const ADepositSite* Deposit : Kit.State->Deposits)
			if (IsValid(Deposit) && Deposit->RegionIndex == Island && IsValid(Deposit->Extractor))
				return true;
		return false;
	}

	void Run()
	{
		if (!Quarantine(Kit, 3000))
		{
			Test->AddError(TEXT("The isolated JEV planner could not spawn"));
			return;
		}
		Kit.Planner->SetActorTickEnabled(false);
		AArmyGroup* Guard = ArmyTestSetup::SpawnGroup(Kit.World, nullptr, -1,
			ArmyTestSetup::FromEnemyHQ(Kit.State, -250.f, 350.f, 100.f));
		if (!Guard)
		{
			Test->AddError(TEXT("JEV fixture squad could not spawn"));
			return;
		}
		Park(*Guard);
		const int32 Home = ArmyTestSetup::RegionAt(Kit.State, Kit.State->EnemyHeadquarters->GetActorLocation());
		const FChainSite Site = FindSite(*Kit.State, Home);
		AMapRegion* Island = FindRegion(*Kit.State, Site.Island);
		AMapRegion* Cut = FindRegion(*Kit.State, Site.Cut);
		if (!Island || !Cut || !IsValid(Island->Anchor) || !IsValid(Cut->Anchor))
		{
			Test->AddError(TEXT("The map has no free deposit two hops from JEV's main through a non-main region"));
			return;
		}
		// JEV holds the island but the region between it and the main is not JEV's: the island is cut off.
		Cut->Anchor->ControllingTeam = -1;
		Island->Anchor->ControllingTeam = 5;
		if (!Test->TestTrue(TEXT("The fixture leaves the island JEV's and the link neutral"),
				Kit.State->GetRegionController(Site.Island) == 5 && Kit.State->GetRegionController(Site.Cut) != 5))
			return;
		for (int32 Pass = 0; Pass < EvaluationPasses; ++Pass)
			Kit.Planner->EvaluatePlan();
		Test->TestFalse(TEXT("JEV never pays for a Drill Rig in a region its main no longer reaches"), IslandBuilt(Site.Island));
		Test->TestTrue(TEXT("Investment continues where the chain holds: JEV built Drill Rigs near its main"), JevRigs() > 0);
		Reconnect(Site);
	}

	void Reconnect(const FChainSite& Site)
	{
		const int32 RigsBefore = JevRigs();
		const int32 PowerBefore = Kit.State->EnemyCommander->Resources;
		FindRegion(*Kit.State, Site.Cut)->Anchor->ControllingTeam = 5;
		for (int32 Pass = 0; Pass < EvaluationPasses && !IslandBuilt(Site.Island); ++Pass)
			Kit.Planner->EvaluatePlan();
		Test->TestTrue(TEXT("Once the link is retaken JEV invests in the island's Drill Rig again"), IslandBuilt(Site.Island));
		const int32 Cost = Kit.State->Content->Building(ArmyTestSetup::ExtractorIndex)->BuildCost;
		Test->TestEqual(TEXT("Resumed investment is paid from JEV's own wallet, one Drill Rig cost each"),
			PowerBefore - Kit.State->EnemyCommander->Resources, (JevRigs() - RigsBefore) * Cost);
	}

	FAutomationTestBase* Test;
	FKit Kit;
	double Started = FPlatformTime::Seconds();
};
}

bool FJevChainDrillRigsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FChainScenario(this));
	return true;
}

#endif
