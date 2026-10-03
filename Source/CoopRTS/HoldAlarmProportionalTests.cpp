#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldProportionalTest, "CoopRTS.Hold.Proportional",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace HoldAlarmProportionalTests
{
class FScenario : public HoldAlarmFixture::FScenario
{
public:
	FScenario(FAutomationTestBase* InTest)
		: HoldAlarmFixture::FScenario(InTest, HoldAlarmFixture::ECase::Proportional) {}

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
		case EStage::Feint:
			return RunFeint(Now);
		case EStage::Expanded:
			return RunExpanded(Now);
		default:
			return bFailed;
		}
	}

private:
	virtual bool AdvanceResponse(double Now) override
	{
		SetStage(EStage::Feint, Now);
		return bFailed;
	}

	bool RunFeint(double Now)
	{
		if (!CheckIdleHolders(FirstResponders))
			return true;
		if (Now - StageStarted < 1.)
			return bFailed;
		ExpandedCenters.Reset();
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			ExpandedCenters.Add(Holder->GetCenter());
		Teleport(Threats[1].Get(), Intrusion + Side * 120.);
		Teleport(Threats[2].Get(), Intrusion - Side * 120.);
		SetStage(EStage::Expanded, Now);
		return bFailed;
	}

	bool RunExpanded(double Now)
	{
		if (Responders().Num() != 4)
			return bFailed;
		for (int32 Index : FirstResponders)
			if (!Check(Holders[Index]->bHoldResponding && Holders[Index]->HoldThreat == FirstTarget.Get(),
					TEXT("Threat growth preserves committed responders and their live target")))
				return true;
		if (!CheckExpandedSelection() || !CheckIdleHolders(Responders()) || !CheckResponseFeed())
			return true;
		EnableWeapons();
		SetStage(EStage::Combat, Now);
		return bFailed;
	}

	bool CheckExpandedSelection()
	{
		TArray<int32> Remaining;
		for (int32 Index = 0; Index < Holders.Num(); ++Index)
			Remaining.Add(Index);
		auto Distance = [this](int32 Index) {
			double Nearest = TNumericLimits<double>::Max();
			for (int32 Threat = 0; Threat < 3; ++Threat)
				Nearest = FMath::Min(Nearest, FVector::DistSquared2D(ExpandedCenters[Index], Threats[Threat]->GetActorLocation()));
			return Nearest;
		};
		Remaining.StableSort([&](int32 A, int32 B) { return Distance(A) < Distance(B); });
		Remaining.RemoveAll([&](int32 Index) { return FirstResponders.Contains(Index); });
		for (int32 Rank = 0; Rank < Remaining.Num(); ++Rank)
			if (!Check(Holders[Remaining[Rank]]->bHoldResponding == (Rank < 2), TEXT("Growing threat adds the nearest remaining holders, not the whole reserve")))
				return false;
		double Sum = 0.;
		for (int32 Index : Responders())
			Sum += Power(*Holders[Index]);
		const double ThreatPower = 3. * Threats[0]->GetDefinition()->UnitCost;
		return Check(Sum >= ThreatPower * 1.25 && Sum - Power(*Holders[Remaining[1]]) < ThreatPower * 1.25,
				   TEXT("All three living intruders contribute Power to the expanded 1.25 response"))
			&& CheckTargets();
	}
};
}

bool FHoldProportionalTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmProportionalTests::FScenario(this));
	return true;
}

#endif
