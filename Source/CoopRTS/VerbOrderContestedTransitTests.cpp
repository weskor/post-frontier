#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderContestedTransitTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::ContestedTransit) {}

private:
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			// A paid all-Brawler force has no incidental long-range Artillery.
			// Pick a real navigable point beside the intermediate anchor, inside
			// capture radius but beyond every member's local weapon reach.
			const FVector Anchor = State->GetRegionAnchor(Intermediate);
			const FVector Forward = (State->GetRegionAnchor(Target) - Anchor).GetSafeNormal2D();
			const FVector Blocker = Anchor + FVector(-Forward.Y, Forward.X, 0.f) * 410.f + FVector(0.f, 0.f, 100.f);
			if (!Check(Region(State, Intermediate)->Contains(Blocker), TEXT("Contesting hostile stands in the real intermediate polygon")))
				return true;
			PutHostile(Blocker);
			Region(State, Intermediate)->Anchor->AdvanceCapture(0.f);
			if (!Check(Region(State, Intermediate)->Anchor->bEnemyPresent
						&& State->GetRegionController(Intermediate) == -1,
					TEXT("Real living hostile contests the neutral intermediate capture point"))
				|| !Issue(EForceVerb::MoveHold, Target))
				return true;
			StartPosition = Force->GetCenter();
			RetreatAttackCount = Attacks();
			SetStage(1);
		}
		bVisitedIntermediate |= Occupies(Intermediate);
		if (Stage == 1 && Holding(Target))
			return Check(bVisitedIntermediate && State->GetRegionController(Intermediate) != 0
					&& Hostile->GetUnits()[0]->IsAlive() && Attacks() == RetreatAttackCount
					&& FVector::Dist2D(StartPosition, Force->GetCenter()) > 500.f,
				TEXT("MoveHold physically passes a contested out-of-range intermediate without chasing or requiring its capture"));
		return false;
	}
};
}

VERB_WORLD_TEST(FVerbContestedTransitTest, "ContestedTransit", ContestedTransit)

#endif
