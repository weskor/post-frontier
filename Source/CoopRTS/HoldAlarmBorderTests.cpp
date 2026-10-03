#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldBorderTest, "CoopRTS.Hold.Border",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace HoldAlarmBorderTests
{
class FScenario : public HoldAlarmFixture::FScenario
{
public:
	FScenario(FAutomationTestBase* InTest)
		: HoldAlarmFixture::FScenario(InTest, HoldAlarmFixture::ECase::Border) {}

	virtual bool Update() override
	{
		double Now = 0.;
		const EStep Preparation = PrepareUpdate(Now);
		if (Preparation != EStep::Continue)
			return Preparation == EStep::Done;
		switch (Stage)
		{
		case EStage::Posts:
			return RunPosts(Now);
		case EStage::Respond:
			return RunRespond(Now);
		case EStage::Combat:
			return RunCombat(Now);
		case EStage::Return:
			return RunReturn(Now);
		default:
			return bFailed;
		}
	}

private:
	virtual bool ObserveCaseHolder(const TWeakObjectPtr<AArmyGroup>& Holder) override
	{
		if (Threats[0].IsValid() && Threats[0]->IsAlive())
		{
			const AArmyUnit* Unit = Holder->GetUnits()[0];
			uint32& Previous = BorderAttacks.FindOrAdd(Holder.Get());
			if (Unit->AttackCount > Previous)
			{
				if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Threats[0]->GetActorLocation()) <= Unit->WeaponRange() + 50.,
						TEXT("Observed outside retaliation shot originates within the actual weapon range")))
					return false;
				bObservedBorderShot = true;
			}
			Previous = Unit->AttackCount;
		}
		return true;
	}

	virtual bool ObserveCaseBorders() override
	{
		if (Stage != EStage::Posts)
			for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
				for (const AArmyUnit* Unit : Holder->GetUnits())
					if (!Check(Region->Contains(Unit->GetActorLocation()) || DistanceToBorder(Unit->GetActorLocation()) < 80.,
							TEXT("Responders stop at the polygon border instead of pursuing outside")))
						return false;
		return true;
	}

	virtual bool CheckCombatForCase() override
	{
		return Check(bObservedBorderShot, TEXT("The outside damaging shooter receives live weapon-range edge retaliation"));
	}

	virtual void BeginReturn() override
	{
		Teleport(Threats[1].Get(), FarOutside);
		OutsideHealth = Threats[1]->GetHealth();
	}

	virtual bool RunReturn(double Now) override
	{
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			if (!Check(!Holder->IsHoldTargetPermitted(*Threats[1], *Region, Holder->GetUnits()[0]->WeaponRange()),
					TEXT("Outside non-damaging hostile is not eligible for retaliation")))
				return true;
		if (!Check(Threats[1]->GetHealth() == OutsideHealth, TEXT("No responder damages the outside non-attacker")))
			return true;
		return HoldAlarmFixture::FScenario::RunReturn(Now);
	}
};
}

bool FHoldBorderTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmBorderTests::FScenario(this));
	return true;
}

#endif
