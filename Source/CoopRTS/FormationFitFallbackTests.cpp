#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "HAL/PlatformTime.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Rules/ArmyGroupPolicy.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationFitSlotFallbackTest, "CoopRTS.Forces.FormationFit.SlotFallback",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace FormationFitFallback
{
// A finished building stands on one slot of the formation at a region's anchor, so that slot has no path. A
// Move & Hold order to the region is accepted: the unit of that slot is sent to the nearest navigable point
// around it inside the region while the others take their own slots.
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 150.)
			return Fail(FString::Printf(TEXT("SlotFallback timed out at stage %d"), Stage));
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
			return Now - StageStarted >= .5 ? Block(*State, Now) : false;
		case 2:
			return Now - StageStarted >= 1.5 ? Order(*State, Now) : false;
		default:
			return Observe(Now);
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

	// Stands a finished building on slot 0 of the formation at the target's anchor.
	bool Block(ACommandGameState& State, double Now)
	{
		for (AMapRegion* Candidate : State.Regions)
			if (IsValid(Candidate) && Candidate->RegionIndex == Target)
				Region = Candidate;
		if (!Region.IsValid())
			return Fail(TEXT("The target region exists"));
		Anchor = State.GetRegionAnchor(Target);
		const ArmyGroupPolicy::FFormation Formation{ false, 6, false };
		const ArmyGroupPolicy::FFit Fit = ArmyGroupPolicy::FitForce(Formation, Region->Polygon, Anchor);
		BlockedSlot = ArmyGroupPolicy::FittedSlot(Formation, Fit, Region->Polygon, 0);
		const FTransform Transform(FVector(BlockedSlot.X, BlockedSlot.Y, 5.));
		Blocker = State.GetWorld()->SpawnActorDeferred<ACommandBuilding>(ACommandBuilding::StaticClass(), Transform,
			Controller.Get(), nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Blocker.IsValid())
			return Fail(TEXT("A building can stand on the formation slot"));
		Blocker->BuildingIndex = ArmyTestSetup::WorkshopIndex;
		Blocker->OwningPlayerState = Controller->GetPlayerState<ACommandPlayerState>();
		Blocker->ConstructionProgress = 1.f;
		Blocker->FinishSpawning(Transform);
		Stage = 2;
		StageStarted = Now;
		return false;
	}

	bool Order(ACommandGameState& State, double Now)
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State.GetWorld());
		FNavLocation Ground;
		if (!Navigation || !ArmyTestSetup::NavigationReady(State.GetWorld()))
			return false;
		if (Navigation->ProjectPointToNavigation(BlockedSlot, Ground, FVector(35., 35., 200.))
			&& FVector::Dist2D(BlockedSlot, Ground.Location) <= 35.)
			return Fail(TEXT("Fixture: the building's navigation cutout must cover the slot it stands on"));
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (!FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, Target).IsAccepted())
			return Fail(TEXT("A region order is accepted although one slot of its formation has no path"));
		// Every member has its own path now; the blocked slot's unit goes to the nearest navigable point around it.
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			const AAIController* AI = Cast<AAIController>(Unit->GetController());
			const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
			if (!Path || Path->GetStatus() != EPathFollowingStatus::Moving)
				return Fail(TEXT("Every member, including the one whose slot is blocked, has an active path"));
			const FVector Goal = Path->GetPathDestination();
			if (Unit->GetCompositionSlot() == 0)
			{
				const double Offset = FVector::Dist2D(Goal, BlockedSlot);
				Test->AddInfo(FString::Printf(TEXT("Blocked slot's unit goes %.0f cm from its slot, %.0f cm from the building"),
					Offset, FVector::Dist2D(Goal, Blocker->GetActorLocation())));
				if (Offset > 335. || Offset < 1. || !Region->Contains(Goal))
					return Fail(TEXT("The blocked slot falls back to a navigable point within 300 cm of it, inside the region"));
				Blocked = Unit;
			}
		}
		if (!Blocked.IsValid())
			return Fail(TEXT("Slot 0 has a unit"));
		Stage = 3;
		StageStarted = Now;
		return false;
	}

	bool Observe(double Now)
	{
		if (!Force.IsValid() || !Region.IsValid() || !Blocker.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("The force, its region and the blocker must survive the scenario"));
		if (!Force->IsHoldingRegion() || Force->HoldRegionIndex != Target || Force->bHoldResponding
			|| ArmyTestSetup::CurrentRegion(Force.Get()) != Target)
			return false;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (Unit->GetVelocity().Size2D() > 5.f)
				return false;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (!Region->Contains(Unit->GetActorLocation()) || Blocker->GetComponentsBoundingBox().IsInsideXY(Unit->GetActorLocation()))
				return Fail(TEXT("Every member ends inside the region and none inside the building"));
		return true;
	}

	FAutomationTestBase* Test;
	double Started, StageStarted = 0.;
	int32 Stage = 0, Target = INDEX_NONE;
	FVector Anchor = FVector::ZeroVector, BlockedSlot = FVector::ZeroVector;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Force;
	TWeakObjectPtr<AMapRegion> Region;
	TWeakObjectPtr<ACommandBuilding> Blocker;
	TWeakObjectPtr<const AArmyUnit> Blocked;
};
}

bool FFormationFitSlotFallbackTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationFitFallback::FScenario(this));
	return true;
}

#endif
