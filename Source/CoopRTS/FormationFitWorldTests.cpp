#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "FormationFitSite.h"
#include "HAL/PlatformTime.h"
#include "NavigationSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationFitBorderHoldTest, "CoopRTS.Forces.FormationFit.BorderHold",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace FormationFitWorld
{
// A six-member force is ordered to Move & Hold a region whose capture anchor sits 130 cm inside one border
// and whose only defend post sits 70 cm inside it. The order is accepted, the force arrives and holds, every
// holder's slot keeps clear of the border and apart from the others, and every holder stands inside the region.
class FBorderHold : public IAutomationLatentCommand
{
public:
	explicit FBorderHold(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 150.)
			return Fail(FString::Printf(TEXT("BorderHold timed out at stage %d"), Stage));
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		Controller = World ? ArmyTestSetup::Controller(World) : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !Controller.IsValid() || ArmyTestSetup::GameSeconds(World) < 3.
			|| !ArmyTestSetup::NavigationReady(World))
			return false;
		const double Now = ArmyTestSetup::GameSeconds(World);
		switch (Stage)
		{
		case 0:
			return Begin(*World, *State, Now);
		case 1:
			return Now - StageStarted >= .5 ? Order(*State, Now) : false;
		default:
			return Observe(*State, Now);
		}
	}

private:
	bool Fail(const FString& Message)
	{
		Test->AddError(Message);
		return true;
	}

	bool Begin(UWorld& World, ACommandGameState& State, double Now)
	{
		// No planner or hostile force may interfere with the fixture.
		for (TActorIterator<AEnemyCommander> It(&World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(&World); It; ++It)
			if (It->IsOpposingArmy())
				It->Destroy();
		const int32 Home = ArmyTestSetup::RegionAt(&State, State.FriendlyHeadquarters->GetActorLocation());
		Force = ArmyTestSetup::SpawnGroup(&World, Controller.Get(), 0, State.GetRegionAnchor(Home) + FVector(0.f, -350.f, 100.f));
		if (!Force.IsValid())
			return Fail(TEXT("A six-member fixture force must spawn"));
		Target = ArmyTestSetup::TravelRegion(Force.Get(), State.EnemyHeadquarters->GetActorLocation());
		if (Target == INDEX_NONE)
			return Fail(TEXT("The home region needs a reachable neighbour that is not the enemy main"));
		Stage = 1;
		StageStarted = Now;
		return false;
	}

	bool Order(ACommandGameState& State, double Now)
	{
		AMapRegion* Region = nullptr;
		for (AMapRegion* Candidate : State.Regions)
			if (IsValid(Candidate) && Candidate->RegionIndex == Target)
				Region = Candidate;
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State.GetWorld());
		FormationFitSite::FSite Site;
		if (!Region || !Navigation || !FormationFitSite::Find(*Navigation, *Region, 130., 70., Site))
			return Fail(TEXT("The target region needs a long border edge with open navigable ground along it"));
		FormationFitSite::Apply(*Region, Site);
		Post = Site.Post;
		TargetRegion = Region;
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (!FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, Target).IsAccepted())
			return Fail(TEXT("A Move & Hold order to a region whose anchor is 130 cm from its border must be accepted"));
		Stage = 2;
		StageStarted = Now;
		return false;
	}

	bool Observe(const ACommandGameState& State, double Now)
	{
		if (!Force.IsValid() || !TargetRegion.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("The force and its six members must survive the scenario"));
		if (!Force->IsHoldingRegion() || Force->HoldRegionIndex != Target || Force->HoldPostIndex == INDEX_NONE
			|| Force->bHoldResponding || ArmyTestSetup::CurrentRegion(Force.Get()) != Target)
		{
			SettledSince = -1.;
			return false;
		}
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (Unit->GetVelocity().Size2D() > 5.f)
			{
				SettledSince = -1.;
				return false;
			}
		if (SettledSince < 0.)
			SettledSince = Now;
		if (Now - SettledSince < 2.)
			return false;
		return Verify();
	}

	bool Verify()
	{
		const AMapRegion& Region = *TargetRegion;
		const TArray<TObjectPtr<AArmyUnit>>& Units = Force->GetUnits();
		if (!FVector::PointsAreNear(Force->HoldPostLocation, Post, 1.f))
			return Fail(TEXT("The force holds at the region's only post"));
		double LeastGoalClearance = TNumericLimits<double>::Max(), LeastGoalGap = TNumericLimits<double>::Max(),
			   LeastStandGap = TNumericLimits<double>::Max();
		for (int32 A = 0; A < Units.Num(); ++A)
		{
			if (Units[A]->PursuitGoal.IsNearlyZero())
				return Fail(TEXT("Every holder was sent to a post slot"));
			if (!Region.Contains(Units[A]->GetActorLocation()))
				return Fail(FString::Printf(TEXT("Holder %d stands outside the region at %s"), A, *Units[A]->GetActorLocation().ToCompactString()));
			LeastGoalClearance = FMath::Min(LeastGoalClearance, FormationFitSite::Clearance(Region, Units[A]->PursuitGoal));
			for (int32 B = A + 1; B < Units.Num(); ++B)
			{
				LeastGoalGap = FMath::Min(LeastGoalGap, FVector::Dist2D(Units[A]->PursuitGoal, Units[B]->PursuitGoal));
				LeastStandGap = FMath::Min(LeastStandGap, FVector::Dist2D(Units[A]->GetActorLocation(), Units[B]->GetActorLocation()));
			}
		}
		Test->AddInfo(FString::Printf(TEXT("Border post 70 cm in: least slot clearance %.0f cm, least slot gap %.0f cm, least standing gap %.0f cm"),
			LeastGoalClearance, LeastGoalGap, LeastStandGap));
		if (LeastGoalClearance < 30.)
			return Fail(TEXT("Every post slot keeps clear of the border instead of clipping onto it"));
		if (LeastGoalGap < 60.)
			return Fail(TEXT("Holders at a border post have distinct slots"));
		if (LeastStandGap < 45.)
			return Fail(TEXT("Holders at a border post stand at distinct points"));
		return true;
	}

	FAutomationTestBase* Test;
	double Started;
	double StageStarted = 0., SettledSince = -1.;
	int32 Stage = 0, Target = INDEX_NONE;
	FVector Post = FVector::ZeroVector;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Force;
	TWeakObjectPtr<AMapRegion> TargetRegion;
};
}

bool FFormationFitBorderHoldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationFitWorld::FBorderHold(this));
	return true;
}

#endif
