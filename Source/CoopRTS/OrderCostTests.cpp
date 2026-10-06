#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "AIController.h"
#include "ArmyGroupPathing.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "FormationHeadingNeck.h"
#include "HAL/PlatformTime.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FOrderCostWorldTest, "CoopRTS.Forces.OrderCost",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace OrderCostTests
{
// An order costs one path query for the force's centre and one per member whose straight line to its slot is
// blocked; a member with a clear line gets a straight path from a navmesh raycast and no query. On open ground
// that is one query for six members; behind two walls with a gap the blocked members still get a real path and
// walk through it to their slots.
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	// The walls are real navigation-affecting actors in the shared test world: they go when the scenario ends.
	~FScenario() override
	{
		for (const TWeakObjectPtr<AActor>& Wall : Walls)
			if (Wall.IsValid())
				Wall->Destroy();
	}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 200.)
			return Fail(FString::Printf(TEXT("Order cost scenario timed out at stage %d"), Stage));
		UWorld* World = ArmyTestSetup::World();
		State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		Controller = World ? ArmyTestSetup::Controller(World) : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !Controller.IsValid() || ArmyTestSetup::GameSeconds(World) < 3. || !ArmyTestSetup::NavigationReady(World))
			return false;
		const double Now = ArmyTestSetup::GameSeconds(World);
		switch (Stage)
		{
		case 0:
			return Begin(*World, Now);
		case 1:
			return OpenGround(*World, Now);
		case 2:
			return Now - StageStarted >= 2.5 ? Walled(*World, Now) : false;
		default:
			return Walk(Now);
		}
	}

private:
	bool Fail(const FString& Message)
	{
		Test->AddError(Message);
		return true;
	}

	bool Begin(UWorld& World, double Now)
	{
		for (TActorIterator<AEnemyCommander> It(&World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(&World); It; ++It)
			if (It->IsOpposingArmy())
				It->Destroy();
		const int32 Home = ArmyTestSetup::RegionAt(State, State->FriendlyHeadquarters->GetActorLocation());
		Force = ArmyTestSetup::SpawnGroup(&World, Controller.Get(), 0, State->GetRegionAnchor(Home) + FVector(0.f, -350.f, 100.f));
		if (!Force.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("A six-member fixture force must spawn"));
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (!FormationHeadingNeck::ChooseNeckRoute(World, *Wallet, *Force, *State, Route))
			return Fail(TEXT("A route with a path exists on the map"));
		Stage = 1;
		StageStarted = Now;
		return false;
	}

	bool OpenGround(UWorld& World, double Now)
	{
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		ArmyGroupPathing::Reset();
		if (!FormationHeadingNeck::Replan(World, *Wallet, *Force, *State, Route))
			return Fail(TEXT("The force has a path on open ground"));
		const ArmyGroupPathing::FQueryStats Open = ArmyGroupPathing::Snapshot();
		Test->AddInfo(FString::Printf(TEXT("Open ground: %lld path queries and %lld straight moves for %d members"), Open.PathQueries, Open.StraightMoves, Force->GetUnits().Num()));
		Check(Open.PathQueries <= 2, FString::Printf(TEXT("An order on open ground costs at most 2 path queries (%lld)"), Open.PathQueries));
		Check(Open.PathQueries + Open.StraightMoves == 7, FString::Printf(TEXT("Every member's move and the centre are accounted for (%lld + %lld)"), Open.PathQueries, Open.StraightMoves));
		const int32 Middle = FMath::Clamp(Route.Points.Num() / 2, 6, Route.Points.Num() - 4);
		FormationHeadingNeck::BuildNeck(World, Route, Middle, NeckGap, Walls);
		Stage = 2;
		StageStarted = Now;
		return false;
	}

	bool Walled(UWorld& World, double Now)
	{
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		ArmyGroupPathing::Reset();
		if (!FormationHeadingNeck::Replan(World, *Wallet, *Force, *State, Route))
			return Fail(TEXT("The force has a path through the gap"));
		const ArmyGroupPathing::FQueryStats Cost = ArmyGroupPathing::Snapshot();
		int32 Blocked = 0;
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			FVector Hit;
			const bool bBlocked = UNavigationSystemV1::NavigationRaycast(&World, Unit->GetNavAgentLocation(), Unit->FormationTarget, Hit);
			Blocked += bBlocked;
			const AAIController* AI = Cast<AAIController>(Unit->GetController());
			const FNavPathSharedPtr Path = AI ? AI->GetPathFollowingComponent()->GetPath() : nullptr;
			if (!Path.IsValid())
				return Fail(TEXT("Every member has a path"));
			if (!bBlocked)
				Check(Path->GetPathPoints().Num() == 2, TEXT("A member with a clear line walks a straight path"));
		}
		Test->AddInfo(FString::Printf(TEXT("Behind walls: %d of %d members blocked; %lld path queries and %lld straight moves"), Blocked, Force->GetUnits().Num(), Cost.PathQueries, Cost.StraightMoves));
		if (Blocked < 1 || Blocked > 5)
			return Fail(FString::Printf(TEXT("Fixture: the walls block some members and not all (%d blocked)"), Blocked));
		Check(Cost.PathQueries == 1 + Blocked, FString::Printf(TEXT("One path query for the centre and one per blocked member (%lld, %d blocked)"), Cost.PathQueries, Blocked));
		Check(Cost.StraightMoves == 6 - Blocked, FString::Printf(TEXT("Every member with a clear line took a straight move (%lld, %d clear)"), Cost.StraightMoves, 6 - Blocked));
		Stage = 3;
		StageStarted = Now;
		return false;
	}

	// The blocked members' paths lead through the gap: every member reaches its slot.
	bool Walk(double Now)
	{
		if (!Force.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("The force survives its march"));
		double Farthest = 0.;
		for (const AArmyUnit* Unit : Force->GetUnits())
			Farthest = FMath::Max(Farthest, FVector::Dist2D(Unit->GetActorLocation(), Unit->FormationTarget));
		if (Farthest > 150. && Now - StageStarted < 90.)
			return false;
		Test->AddInfo(FString::Printf(TEXT("After %.0f s the farthest member is %.0f cm from its slot"), Now - StageStarted, Farthest));
		Check(Farthest <= 150., FString::Printf(TEXT("Every member, blocked or not, reaches its slot (farthest %.0f cm)"), Farthest));
		return true;
	}

	void Check(bool bOk, const FString& Message)
	{
		if (!bOk)
			Test->AddError(Message);
	}

	FAutomationTestBase* Test;
	double Started, StageStarted = 0.;
	int32 Stage = 0;
	ACommandGameState* State = nullptr;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Force;
	FormationHeadingNeck::FRoute Route;
	TArray<TWeakObjectPtr<AActor>> Walls;
	static constexpr double NeckGap = 160.;
};
}

bool FOrderCostWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(OrderCostTests::FScenario(this));
	return true;
}
#endif
