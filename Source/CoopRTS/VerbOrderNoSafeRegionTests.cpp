#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderNoSafeRegionTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::NoSafeRegion) {}

private:
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			for (const AMapRegion* Candidate : State->Regions)
				if (!Check(State->GetRegionController(Candidate->RegionIndex) != 0 || Candidate->RegionIndex == Home,
						TEXT("No-safe fixture has only its HQ controlled")))
					return true;
			if (!KillTo(3))
				return true;
			PutHostile(State->GetRegionAnchor(Home) + FVector(0.f, 0.f, 100.f));
			if (!Issue(EForceVerb::Retreat))
				return true;
			if (!Check(Force->Verb == EForceVerb::Retreat && Force->WaypointRegionIndex == Home,
					TEXT("No-safe Retreat chooses the hostile HQ fallback")))
				return true;
			SetStage(1);
		}
		if (Force->Status != EForceStatus::Refilling)
			return false;
		AArmyUnit* Shooter = Force->GetUnits()[0];
		AArmyUnit* Victim = Hostile->GetUnits()[0];
		PutHostile(Shooter->GetActorLocation() + FVector(70.f, 0.f, 0.f));
		const int32 Before = Victim->GetHealth();
		Shooter->NextAttackTime = 0.f;
		TickForce();
		Shooter->Tick(.25f);
		return Check(Force->GetJoinedCount() == 3 && !Producer->bProductionEnabled
				&& Victim->GetHealth() < Before && Force->Verb == EForceVerb::Retreat,
			TEXT("Paused Retreat refill at unsafe HQ fallback actually acquires and fires on hostiles"));
	}
};
}

VERB_WORLD_TEST(FVerbNoSafeRegionTest, "NoSafeHQFallback", NoSafeRegion)

#endif
