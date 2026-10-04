#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include <algorithm>

#include "CapturePoint.h"
#include "JevReleaseWorldFixture.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/JevThreatPolicy.h"

// Split-Brain Cut world scenarios on Habitable Zone v2, over the quarantined JEV of JevReleaseWorldFixture.h. The threat
// reads its authored neck pairs from the map's region tags, so every scenario runs on that map.
namespace JevThreatKit
{
using namespace JevWorldKit;

inline AMapRegion* RegionActor(const ACommandGameState& State, int32 Index)
{
	for (AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex == Index)
			return Region;
	return nullptr;
}

// The planner's view of the map as the rules read it: existence, mains, neighbours and, where an anchor stands, control.
inline JevPlanner::FWorld PlannerWorld(const ACommandGameState& State)
{
	JevPlanner::FWorld World;
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region) || Region->RegionIndex < 0 || Region->RegionIndex >= ForceOrders::MaxRegions)
			continue;
		JevPlanner::FRegion& Out = World.Regions[Region->RegionIndex];
		Out.bExists = true;
		Out.bMain = Region->RegionRole == ERegionRole::Main;
		for (const int32 Neighbour : Region->Neighbours)
			if (Neighbour >= 0 && Neighbour < ForceOrders::MaxRegions)
				Out.Neighbours |= uint64(1) << Neighbour;
		if (Out.bMain)
			(Region->HomeTeam == 5 ? World.Home : World.EnemyHome) = Region->RegionIndex;
	}
	return World;
}

// The authored pairs on the map, read from the region tags as the executor reads them.
inline TArray<JevThreat::FPair> AuthoredPairs(const ACommandGameState& State)
{
	TArray<JevThreat::FTaggedRegion> Tagged;
	for (const AMapRegion* Region : State.Regions)
		if (IsValid(Region))
			for (const FName& Tag : Region->Tags)
				if (const int32 Pair = JevThreat::PairOfTag(Tag.ToString()); Pair != INDEX_NONE)
					Tagged.Add({ Region->RegionIndex, Pair });
	return JevThreat::BuildPairs(Tagged);
}

// A scenario with a second human commander (or none), region control and Fortify set by the test. Everything it changes in
// the shared world is put back when it ends.
class FThreatScenario : public FScenario
{
public:
	FThreatScenario(FAutomationTestBase* InTest, bool bInCoop) : FScenario(InTest), bCoop(bInCoop) {}

	bool Update() override
	{
		const bool bDone = FScenario::Update();
		if (bDone)
			Restore();
		return bDone;
	}

protected:
	static bool Has(std::initializer_list<int32> List, int32 Value)
	{
		return std::find(List.begin(), List.end(), Value) != List.end();
	}

	// Makes the roster two commanders when bCoop. Regions in Human are the humans', Jev JEV's, every other region neutral.
	bool Arrange(std::initializer_list<int32> Human, std::initializer_list<int32> Jev = {})
	{
		if (bCoop)
		{
			Other = Kit.World->SpawnActor<ACommandPlayerState>();
			if (!Other)
				return Fail(TEXT("Second commander fixture could not spawn"));
			Other->TeamIndex = 0;
			Other->CommanderIndex = Kit.Wallet->CommanderIndex == 0 ? 1 : 0;
			Kit.State->AddPlayerState(Other);
		}
		for (AMapRegion* Region : Kit.State->Regions)
			if (IsValid(Region) && IsValid(Region->Anchor))
			{
				const int32 Team = Has(Jev, Region->RegionIndex) ? 5 : Has(Human, Region->RegionIndex) ? 0
																									   : INDEX_NONE;
				Region->Anchor->ControllingTeam = Team;
				Region->Anchor->CaptureProgress = Team == INDEX_NONE ? 0.f : 1.f;
				Region->FortifyTeam = -1;
				Region->FortifyCaster = -1;
				Region->FortifyExpiresAt = 0.f;
			}
		Kit.Wallet->FortifyReadyAt = 0.f;
		SequenceBase = 0;
		if (const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Kit.State))
			for (const FObjectiveEvent& Event : Announcer->GetEvents())
				SequenceBase = FMath::Max(SequenceBase, Event.Sequence);
		JevMain = ArmyTestSetup::RegionAt(Kit.State, Kit.State->EnemyHeadquarters->GetActorLocation());
		return true;
	}

	// JEV's living forces under an Attack order on one of Regions.
	TArray<AArmyGroup*> ForcesTargeting(std::initializer_list<int32> Regions) const
	{
		TArray<AArmyGroup*> Out;
		for (AArmyGroup* Force : EnemyForces(Kit.World))
			if (Force->Verb == EForceVerb::Attack && Has(Regions, Force->TargetRegionIndex))
				Out.Add(Force);
		return Out;
	}

	static void DestroyForces(UWorld* World, TFunctionRef<bool(const AArmyGroup&)> Pick)
	{
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			if (Pick(**It))
				It->Destroy();
	}

	// Announcer events with this id raised since Arrange, oldest first.
	TArray<const FObjectiveEvent*> NewEvents(FName Id) const
	{
		TArray<const FObjectiveEvent*> Out;
		if (const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(Kit.State))
			for (const FObjectiveEvent& Event : Announcer->GetEvents())
				if (Event.Sequence > SequenceBase && Event.Id == Id)
					Out.Add(&Event);
		return Out;
	}

	const FJevCutPlan* CutAt(int32 Region) const
	{
		return Release().Cuts.FindByPredicate([Region](const FJevCutPlan& Cut) { return Cut.Target == Region; });
	}

	const FJevWaveEvent* CutEvent(int32 Region) const
	{
		return Release().Waves.FindByPredicate(
			[Region](const FJevWaveEvent& Event) { return Event.bCut && Event.TargetRegion == Region; });
	}

	int32 CutEvents() const
	{
		int32 Count = 0;
		for (const FJevWaveEvent& Event : Release().Waves)
			Count += Event.bCut;
		return Count;
	}

	bool bCoop;
	ACommandPlayerState* Other = nullptr;
	int32 JevMain = INDEX_NONE;
	// The newest announcer event before Arrange; the ring is shared by every scenario in the world.
	int32 SequenceBase = 0;

private:
	// Neutral regions, no Fortify, no second commander and no force: what the next scenario expects of the shared world.
	void Restore()
	{
		if (!Kit.State || !Kit.World)
			return;
		DestroyForces(Kit.World, [](const AArmyGroup&) { return true; });
		if (IsValid(Other))
			Other->Destroy();
		for (AMapRegion* Region : Kit.State->Regions)
			if (IsValid(Region))
			{
				Region->FortifyTeam = -1;
				Region->FortifyCaster = -1;
				Region->FortifyExpiresAt = 0.f;
				if (IsValid(Region->Anchor))
					Region->Anchor->ControllingTeam = INDEX_NONE;
			}
		Kit.Wallet->FortifyReadyAt = 0.f;
	}
};

// The timeline every threat scenario shares: JEV's v1.1 and v1.2 waves launch as the clock jumps to 329 s and are cleared
// (they are not under test), the plans publish 30 s ahead, the forces launch at 360 s. Subclasses place the humans and
// read the moments; each hook returns true to end the scenario.
class FFlowScenario : public FThreatScenario
{
public:
	using FThreatScenario::FThreatScenario;

protected:
	// Places the humans' regions and forces before the clock moves.
	virtual bool Place() = 0;
	// The plans were seen published, or (bSeen false) none appeared within three game seconds of 331 s.
	virtual bool OnPublished(bool bSeen) = 0;
	// 359.8 s: the forces have not launched.
	virtual bool BeforeLaunch() { return false; }
	// The v2.0 release launched at 360 s.
	virtual bool OnLaunched() = 0;
	// Every frame after OnLaunched returned false.
	virtual bool Afterwards() { return true; }

	bool Prepare() override
	{
		if (!Place())
			return true;
		SkipTo(329.f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		switch (Stage)
		{
		case 1:
			if (!WaveLaunched(2))
				return false;
			DestroyForces(Kit.World, [](const AArmyGroup&) { return true; });
			SkipTo(331.f);
			Enter(2);
			return false;
		case 2: {
			const bool bSeen = !Release().Cuts.IsEmpty();
			if (!bSeen && InStage() < 3.)
				return false;
			PublishedAt = Kit.Planner->GetMatchSeconds();
			if (OnPublished(bSeen))
				return true;
			SkipTo(359.3f);
			Enter(3);
			return false;
		}
		case 3:
			if (InStage() < .5)
				return false;
			if (BeforeLaunch())
				return true;
			SkipTo(360.8f);
			Enter(4);
			return false;
		case 4:
			if (!WaveLaunched(3))
				return false;
			if (OnLaunched())
				return true;
			Enter(5);
			return false;
		default:
			return Afterwards();
		}
	}

	// Match seconds at which the plans were first seen.
	float PublishedAt = 0.f;
};
}

#endif
