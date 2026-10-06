#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

// The first region on the way is Open ground. The leading members enter it, and gain Open's speed, before the
// rest: the bonus must apply per member and the force must still arrive together.
namespace VerbOrderOpenMemberTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	bool RunScenario() override
	{
		if (Stage == 0)
		{
			Region(State, Intermediate)->Trait = ERegionTrait::Open;
			if (!Issue(EForceVerb::MoveHold, Intermediate))
				return true;
			SetStage(1);
		}
		if (Force->Status == EForceStatus::Marching)
		{
			int32 Bonus = 0, Plain = 0;
			for (const AArmyUnit* Unit : Force->GetUnits())
			{
				const UArmyUnitMovement* Movement = Cast<UArmyUnitMovement>(Unit->GetCharacterMovement());
				if (!Check(Movement != nullptr, TEXT("Members use the trait-aware movement component")))
					return true;
				Movement->TraitSpeedMultiplier > 1.1f ? ++Bonus : ++Plain;
			}
			bMixed |= Bonus > 0 && Plain > 0;
			MaxSpread = FMath::Max(MaxSpread, Force->GetMarchSpread());
		}
		if (Stage == 1 && Force->IsHoldingRegion() && Force->TargetRegionIndex == Intermediate)
		{
			const float Bound = Force->GetFormationRadius() + 170.f;
			Test->AddInfo(FString::Printf(TEXT("Largest spread %.0f cm, bound %.0f cm"), MaxSpread, Bound));
			return Check(bMixed, TEXT("Some members were in Open ground with the bonus while others were not"))
				&& Check(MaxSpread <= Bound, TEXT("The force stayed within its formation radius plus 170 cm while the bonus applied member by member"));
		}
		return false;
	}

	bool bMixed = false;
	float MaxSpread = 0.f;
};
}

VERB_WORLD_TEST(FVerbOpenMemberTest, "OpenMember", OpenMember)

#endif
