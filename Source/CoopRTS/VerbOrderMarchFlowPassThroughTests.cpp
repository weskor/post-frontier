#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

// A three-region march First, Second, Target from home. Where First and Second are already the team's, the force is
// ordered straight past First to Second (the leg into the target region stays its own leg) and never takes First
// as a waypoint. Where First is not, the force still goes to it first, captures it, and only then moves on.
namespace VerbOrderPassThroughTests
{
using namespace VerbOrderTests;

template <bool bControlled>
class FScenarioT : public FScenarioBase
{
public:
	FScenarioT(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	// Three neutral anchored regions in a row from home, by breadth-first search in ascending index order.
	bool FindChain()
	{
		TMap<int32, int32> Parent;
		TArray<int32> Pending{ Home };
		Parent.Add(Home, INDEX_NONE);
		for (int32 Cursor = 0; Cursor < Pending.Num(); ++Cursor)
		{
			const AMapRegion* At = Region(State, Pending[Cursor]);
			TArray<int32> Neighbours = At->Neighbours;
			Neighbours.Sort();
			for (const int32 Next : Neighbours)
			{
				const AMapRegion* Candidate = Region(State, Next);
				if (!Candidate || Parent.Contains(Next) || Candidate->HomeTeam >= 0 || !IsValid(Candidate->Anchor)
					|| State->GetRegionController(Next) != -1)
					continue;
				Parent.Add(Next, Pending[Cursor]);
				Pending.Add(Next);
			}
		}
		// A region three hops from home has First two parents up and Second one parent up.
		for (const int32 Candidate : Pending)
		{
			const int32 Second = Parent[Candidate];
			const int32 First = Second != INDEX_NONE && Second != Home ? Parent[Second] : INDEX_NONE;
			if (First != INDEX_NONE && First != Home && Parent[First] == Home)
			{
				ChainFirst = First;
				ChainSecond = Second;
				ChainTarget = Candidate;
				return true;
			}
		}
		return false;
	}

	bool RunScenario() override
	{
		if (Stage == 0)
		{
			if (!Check(FindChain(), TEXT("The map has a neutral three-region chain from home")))
				return true;
			Region(State, ChainSecond)->Anchor->ControllingTeam = 0;
			Region(State, ChainSecond)->Anchor->CaptureProgress = 1.f;
			if (bControlled)
			{
				Region(State, ChainFirst)->Anchor->ControllingTeam = 0;
				Region(State, ChainFirst)->Anchor->CaptureProgress = 1.f;
			}
			if (!Issue(EForceVerb::MoveHold, ChainTarget))
				return true;
			SerialAtOrder = Force->OrderSerial;
			OrderedAt = ArmyTestSetup::GameSeconds(GameWorld);
			if (!Check(Force->WaypointRegionIndex == (bControlled ? ChainSecond : ChainFirst),
					bControlled ? TEXT("A controlled first region is passed: the first waypoint is the second region")
								: TEXT("An uncontrolled first region is the first waypoint")))
				return true;
			SetStage(1);
		}
		bTookFirst |= Force->WaypointRegionIndex == ChainFirst;
		if (!bControlled && Force->WaypointRegionIndex != ChainFirst && !bCapturedFirst)
		{
			bCapturedFirst = State->GetRegionController(ChainFirst) == 0;
			if (!Check(bCapturedFirst, TEXT("The force moves on from an uncontrolled region only once it controls it")))
				return true;
		}
		if (Stage == 1 && Force->IsHoldingRegion() && Force->TargetRegionIndex == ChainTarget && State->GetRegionController(ChainTarget) == 0)
		{
			const double Elapsed = ArmyTestSetup::GameSeconds(GameWorld) - OrderedAt;
			const int32 Orders = static_cast<int32>(Force->OrderSerial - SerialAtOrder);
			Test->AddInfo(FString::Printf(TEXT("%d orders in %.1f s"), Orders, Elapsed));
			if (bControlled)
				return Check(!bTookFirst && Orders <= 3 + FMath::CeilToInt(Elapsed / MovementProgressPolicy::RepeatOrderSeconds),
					TEXT("The march never takes the controlled first region as a waypoint and is not re-ordered at its anchor"));
			return Check(bTookFirst && bCapturedFirst, TEXT("The uncontrolled first region was the waypoint and was captured on the way"));
		}
		return false;
	}

	int32 ChainFirst = INDEX_NONE, ChainSecond = INDEX_NONE, ChainTarget = INDEX_NONE;
	uint32 SerialAtOrder = 0;
	double OrderedAt = 0.;
	bool bTookFirst = false, bCapturedFirst = false;
};
}

namespace VerbOrderPassCaptureTests
{
using FScenario = VerbOrderPassThroughTests::FScenarioT<false>;
}

namespace VerbOrderPassThroughTests
{
using FScenario = FScenarioT<true>;
}

VERB_WORLD_TEST(FVerbPassThroughTest, "PassThrough", PassThrough)
VERB_WORLD_TEST(FVerbPassCaptureTest, "PassCapture", PassCapture)

#endif
