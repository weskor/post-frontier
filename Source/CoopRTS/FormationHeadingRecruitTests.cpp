#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SupplyDeliveryFixture.h"
#include "Rules/ArmyGroupPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingRecruitTest, "CoopRTS.Forces.FormationHeading.Recruit",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace FormationHeadingRecruit
{
// A two-member force is marching the first leg (to the Neck region, an intermediate leg of its Move & Hold to
// Far) in column when a third recruit is delivered. It joins the file's tail: one column spacing behind the
// rearmost planned target, not on its composition slot of the box, which may sit inside the file.
class FScenario : public SupplyTests::FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;

protected:
	bool Run() override
	{
		switch (Stage)
		{
		case 0:
			Produce();
			SetStage(1);
			return false;
		case 1:
			if (StageSeconds() < .5)
				return false;
			Produce(); // The second member arrives after the delivery delay.
			SetStage(2);
			return false;
		case 2:
			if (Joined() < 2)
				return !Check(StageSeconds() < 12., TEXT("The second recruit joins"));
			if (StageSeconds() < 6.)
				return false;
			Produce(); // The third recruit's delay (4 s at the producer's region) starts now: it lands just after the march begins.
			SetStage(3);
			return false;
		case 3:
			return StageSeconds() < 3. ? false : Begin();
		default:
			return Verify();
		}
	}

private:
	bool Begin()
	{
		if (!Check(FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, Far).IsAccepted(),
				TEXT("The force accepts its march to Far")))
			return true;
		SetStage(4);
		return false;
	}

	bool Verify()
	{
		if (Joined() < 3)
			return !Check(StageSeconds() < 14., TEXT("The third recruit joins within the delivery delay"));
		TArray<const AArmyUnit*> Others;
		const AArmyUnit* Recruit = nullptr;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit->IsAlive())
				Others.Add(Unit);
		// The recruit is the member that joined last: the one with the highest composition slot.
		for (const AArmyUnit* Unit : Others)
			if (!Recruit || Unit->GetCompositionSlot() > Recruit->GetCompositionSlot())
				Recruit = Unit;
		TArray<FVector2D> File;
		const ArmyGroupPolicy::FLegMemory* Leg = nullptr;
		for (const AArmyUnit* Unit : Others)
			if (Unit != Recruit)
			{
				File.Add(FVector2D(Unit->FormationTarget));
				Leg = &Unit->FormationMemory;
			}
		Test->AddInfo(FString::Printf(TEXT("At the third recruit: leg memory %d column %d, waypoint region %d (Neck %d), file of %d, force status %d, destination %s"),
			Leg != nullptr, Leg && Leg->bColumn, Force->WaypointRegionIndex, Neck, File.Num(), static_cast<int32>(Force->Status), *Force->Destination.ToCompactString()));
		if (!Check(Recruit && Leg && Leg->bColumn && Force->WaypointRegionIndex == Neck && File.Num() == 2,
				TEXT("Fixture: the third recruit joins while the force is on its column leg to the Neck region")))
			return true;
		const FVector2D Tail = ArmyGroupPolicy::ColumnTail(File, Leg->Yaw);
		const FVector2D Joined2D(Recruit->FormationTarget);
		const double Miss = FVector2D::Distance(Tail, Joined2D);
		double Nearest = TNumericLimits<double>::Max();
		for (const FVector2D& Target : File)
			Nearest = FMath::Min(Nearest, FVector2D::Distance(Target, Joined2D));
		Test->AddInfo(FString::Printf(TEXT("Recruit joined the column: target %.0f cm from the tail slot, %.0f cm from the nearest file member (file spacing %.0f cm), column memory %d"),
			Miss, Nearest, ArmyGroupPolicy::ColumnSpacing, Recruit->FormationMemory.bColumn));
		Check(Miss <= 45., TEXT("The recruit's target is the file's tail, one column spacing behind the rearmost planned slot"));
		Check(Nearest >= ArmyGroupPolicy::ColumnFloorSpacing, TEXT("The recruit does not stand inside the file"));
		Check(Recruit->FormationMemory.bColumn, TEXT("The recruit carries the column leg's memory"));
		return true;
	}
};
}

bool FFormationHeadingRecruitTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationHeadingRecruit::FScenario(this));
	return true;
}
#endif
