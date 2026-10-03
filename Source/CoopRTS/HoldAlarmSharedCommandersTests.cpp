#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldSharedCommandersTest, "CoopRTS.Hold.SharedCommanders",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace HoldAlarmSharedCommandersTests
{
class FScenario : public HoldAlarmFixture::FScenario
{
public:
	FScenario(FAutomationTestBase* InTest)
		: HoldAlarmFixture::FScenario(InTest, HoldAlarmFixture::ECase::SharedCommanders) {}

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
	virtual bool ChooseIntrusion() override
	{
		// Pick an actual map-nav intrusion whose nearest pair belongs to different
		// commanders, so separate commander-local quotas would mobilize too many.
		TArray<FVector> Candidates;
		Candidates.Add(Intrusion);
		for (int32 A = 0; A < Holders.Num(); ++A)
			for (int32 B = A + 1; B < Holders.Num(); ++B)
				if (Holders[A]->GetOwningPlayerState() != Holders[B]->GetOwningPlayerState())
					Candidates.Add((InitialCenters[A] + InitialCenters[B]) * .5);
		for (const FVector& Candidate : Candidates)
		{
			FVector Point;
			if (!Project(Candidate, Point) || !Region->Contains(Point))
				continue;
			TArray<int32> Sorted = SortedByDistance(Point, InitialCenters);
			if (FVector::Dist2D(Point, InitialCenters[Sorted[0]]) <= State->Content->Unit(UnitIndex)->Range + 250.)
				continue;
			if (Holders[Sorted[0]]->GetOwningPlayerState() != Holders[Sorted[1]]->GetOwningPlayerState())
			{
				Intrusion = Point;
				return true;
			}
		}
		return Check(false, TEXT("Authored posts permit a nearest response pair spanning both commanders"));
	}
};
}

bool FHoldSharedCommandersTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmSharedCommandersTests::FScenario(this));
	return true;
}

#endif
