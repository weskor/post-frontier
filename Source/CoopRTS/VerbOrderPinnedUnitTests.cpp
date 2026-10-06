#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"
#include "VerbOrderProgressSupport.h"

// One member walled in at the start of a two-step MoveHold: it settles within the bound, and the other
// five still arrive, capture and hold.
namespace VerbOrderPinnedUnitTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	// Idle windows end at 1.5 s steps, so the unit settles a window after SettleIdleSeconds at the latest;
	// the second is the force tick that opens the first window.
	static constexpr double SettleBound = MovementProgressPolicy::SettleIdleSeconds + MovementProgressPolicy::SampleSeconds + 1.;

	bool RunScenario() override
	{
		if (Stage == 0)
		{
			Pinned = Force->GetUnits()[0];
			PinnedStart = Pinned->GetActorLocation();
			if (!Check(BuildCage(*GameWorld, PinnedStart, 90.f, 30.f), TEXT("The engine cube builds a wall around one member"))
				|| !Issue(EForceVerb::MoveHold, Target))
				return true;
			OrderedAt = ArmyTestSetup::GameSeconds(GameWorld);
			SetStage(1);
		}
		if (!Check(Pinned.IsValid(), TEXT("The walled member survives")))
			return true;
		if (!bSettled && Force->IsUnitSettled(*Pinned))
		{
			bSettled = true;
			const double Elapsed = ArmyTestSetup::GameSeconds(GameWorld) - OrderedAt;
			if (!Check(Elapsed <= SettleBound && FVector::Dist2D(Pinned->GetActorLocation(), PinnedStart) < 100.f,
					TEXT("The walled member settles within the bound, still in its wall")))
				return true;
		}
		if (Force->Verb != EForceVerb::MoveHold || Force->TargetRegionIndex != Target || Force->Status != EForceStatus::Holding
			|| State->GetRegionController(Target) != 0)
			return false;
		if (!Check(bSettled && Force->GetSettledUnitCount() >= 1, TEXT("The walled member was settled before the force held")))
			return true;
		const AMapRegion* TargetRegion = Region(State, Target);
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (Unit != Pinned.Get() && !Check(TargetRegion->Contains(Unit->GetActorLocation()), TEXT("The five free members hold the target region")))
				return true;
		return Check(FVector::Dist2D(Pinned->GetActorLocation(), PinnedStart) < 100.f && !TargetRegion->Contains(Pinned->GetActorLocation()),
			TEXT("The force reached Holding without the walled member, which never left its wall"));
	}

	TWeakObjectPtr<AArmyUnit> Pinned;
	FVector PinnedStart = FVector::ZeroVector;
	double OrderedAt = 0.;
	bool bSettled = false;
};
}

VERB_WORLD_TEST(FVerbPinnedUnitTest, "PinnedUnit", PinnedUnit)

#endif
