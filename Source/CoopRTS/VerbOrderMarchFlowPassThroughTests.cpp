#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

// A two-region march. Where the first region is already the team's, the force is ordered straight to the second
// and never takes the first as a waypoint. Where it is not, the force still goes to it first, captures it, and
// only then moves on.
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
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			if (bControlled)
			{
				ACapturePoint* Anchor = Region(State, Intermediate)->Anchor;
				Anchor->ControllingTeam = 0;
				Anchor->CaptureProgress = 1.f;
				if (!Check(State->GetRegionController(Intermediate) == 0, TEXT("The first region is the team's before the march")))
					return true;
			}
			if (!Issue(EForceVerb::MoveHold, Target))
				return true;
			SerialAtOrder = Force->OrderSerial;
			OrderedAt = ArmyTestSetup::GameSeconds(GameWorld);
			if (!Check(Force->WaypointRegionIndex == (bControlled ? Target : Intermediate),
					bControlled ? TEXT("A controlled first region is passed: the first waypoint is the second region")
								: TEXT("An uncontrolled first region is the first waypoint")))
				return true;
			SetStage(1);
		}
		bTookIntermediate |= Force->WaypointRegionIndex == Intermediate;
		if (!bControlled && Force->WaypointRegionIndex == Target && !bCapturedFirst)
		{
			bCapturedFirst = State->GetRegionController(Intermediate) == 0;
			if (!Check(bCapturedFirst, TEXT("The force moves on from an uncontrolled region only once it controls it")))
				return true;
		}
		if (Stage == 1 && Holding(Target))
		{
			const double Elapsed = ArmyTestSetup::GameSeconds(GameWorld) - OrderedAt;
			const int32 Orders = static_cast<int32>(Force->OrderSerial - SerialAtOrder);
			Test->AddInfo(FString::Printf(TEXT("%d orders in %.1f s"), Orders, Elapsed));
			if (bControlled)
				return Check(!bTookIntermediate && Orders <= 3 + FMath::CeilToInt(Elapsed / MovementProgressPolicy::RepeatOrderSeconds),
					TEXT("The march never takes the controlled first region as a waypoint and is not re-ordered at its anchor"));
			return Check(bTookIntermediate && bCapturedFirst, TEXT("The uncontrolled first region was the waypoint and was captured on the way"));
		}
		return false;
	}

	uint32 SerialAtOrder = 0;
	double OrderedAt = 0.;
	bool bTookIntermediate = false, bCapturedFirst = false;
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
