#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"
#include "Components/CapsuleComponent.h"
#include "Rules/MarchSpeedPolicy.h"

// One member starts 400-700 cm behind its force on the way to the first region. It must run faster than the
// force, the others must not, and the gap along the march must shrink while the force is still under way.
namespace VerbOrderCatchUpTests
{
using namespace VerbOrderTests;

class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	// Planar lag of the delayed member behind its slot along the march, against the mean of the force.
	float DelayedLag() const
	{
		TArray<MarchSpeedPolicy::FMember, TInlineAllocator<8>> Members;
		int32 DelayedIndex = INDEX_NONE;
		FVector Center = FVector::ZeroVector;
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			if (Unit == Delayed.Get())
				DelayedIndex = Members.Num();
			// The member's own planned slot (a column or assigned slot), as the march measures lag.
			Members.Add({ FVector2D(Unit->GetActorLocation()), FVector2D(Unit->FormationTarget) });
			Center += Unit->GetActorLocation();
		}
		const FVector2D Heading = (FVector2D(Force->Destination) - FVector2D(Center / Members.Num())).GetSafeNormal();
		return MarchSpeedPolicy::Lag(Members, DelayedIndex, Heading);
	}

	bool Delay()
	{
		Delayed = Force->GetUnits()[3];
		const FVector Heading = (State->GetRegionAnchor(Intermediate) - Force->GetCenter()).GetSafeNormal2D();
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GameWorld);
		for (const float Distance : { 700.f, 550.f, 400.f })
		{
			FNavLocation Ground;
			const FVector Candidate = Delayed->GetActorLocation() - Heading * Distance;
			if (Navigation && Navigation->ProjectPointToNavigation(Candidate, Ground, FVector(40.f, 40.f, 200.f))
				&& FVector::Dist2D(Candidate, Ground.Location) <= 40.f && Region(State, Home)->Contains(Ground.Location))
			{
				Delayed->SetActorLocation(Ground.Location + FVector(0.f, 0.f, Delayed->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.f),
					false, nullptr, ETeleportType::TeleportPhysics);
				return true;
			}
		}
		return false;
	}

	bool RunScenario() override
	{
		const double Now = ArmyTestSetup::GameSeconds(GameWorld);
		if (Stage == 0)
		{
			if (!Check(Delay(), TEXT("Open navigable ground 400 cm or more behind the member inside the home region")))
				return true;
			if (!Issue(EForceVerb::MoveHold, Intermediate))
				return true;
			SetStage(1);
		}
		if (!Check(Delayed.IsValid(), TEXT("The delayed member survives")))
			return true;
		if (Stage == 1 && Now - StageGameStarted >= 1.)
		{
			StartLag = DelayedLag();
			const float Cap = Force->GetMarchSpeed();
			const float DelayedSpeed = Delayed->GetCharacterMovement()->MaxWalkSpeed;
			float FastestOther = 0.f;
			for (const AArmyUnit* Unit : Force->GetUnits())
				if (Unit != Delayed.Get())
					FastestOther = FMath::Max(FastestOther, Unit->GetCharacterMovement()->MaxWalkSpeed);
			Test->AddInfo(FString::Printf(TEXT("Delayed lag %.0f cm; speeds: delayed %.0f, fastest other %.0f, force %.0f"),
				StartLag, DelayedSpeed, FastestOther, Cap));
			if (!Check(StartLag > MarchSpeedPolicy::BandHalfWidth + MarchSpeedPolicy::RampLength / 2.f,
					TEXT("The delay puts the member well behind its slot")))
				return true;
			if (!Check(DelayedSpeed >= Cap * 1.1f && DelayedSpeed <= Cap * MarchSpeedPolicy::MaxCatchUp + .5f,
					TEXT("The delayed member runs 10 to 15 percent above the force's speed")))
				return true;
			if (!Check(FastestOther <= Cap * 1.001f, TEXT("The members in formation keep the force's speed")))
				return true;
			SetStage(2);
		}
		if (Stage == 2 && Now - StageGameStarted >= 6.)
		{
			const float EndLag = DelayedLag();
			Test->AddInfo(FString::Printf(TEXT("Lag %.0f cm after 6 s, from %.0f"), EndLag, StartLag));
			return Check(Force->Status == EForceStatus::Marching || Force->IsHoldingRegion(), TEXT("The force is under way or already held"))
				&& Check(EndLag <= StartLag - 150.f || EndLag <= MarchSpeedPolicy::BandHalfWidth,
					TEXT("The delayed member closes at least 150 cm of its lag in 6 s, or is back inside the band"));
		}
		return false;
	}

	TWeakObjectPtr<AArmyUnit> Delayed;
	float StartLag = 0.f;
};
}

VERB_WORLD_TEST(FVerbCatchUpTest, "CatchUp", CatchUp)

#endif
