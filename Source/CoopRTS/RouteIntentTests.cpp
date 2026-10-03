#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "Commands/OrderGraph.h"
#include "HUD/ForceRoutePresentation.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FRouteIntentWorldTest, "CoopRTS.Forces.RouteIntent",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

class FRouteIntentScenario : public IAutomationLatentCommand
{
public:
	explicit FRouteIntentScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 35.)
		{
			Test->AddError(TEXT("Route fixture never reached a march waypoint"));
			return true;
		}
		UWorld* World = ArmyTestSetup::World();
		if (!World || !ArmyTestSetup::CombatActors(World)
			|| (!Force.IsValid() && !ArmyTestSetup::NavigationReady(World)))
			return false;
		ACommandPlayerController* PC = ArmyTestSetup::Controller(World);
		const ACommandGameState* State = World->GetGameState<ACommandGameState>();
		if (!Force.IsValid())
		{
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->GetOwner() == PC && It->GetArmyIndex() == 0)
					Force = *It;
			if (!Force.IsValid())
				return false;
			Home = ForceOrderGraph::SourceRegion(*Force, *State);
			Away = ArmyTestSetup::TravelRegion(Force.Get(), State->EnemyHeadquarters->GetActorLocation());
			Force->RetreatThreshold = ERetreatThreshold::Never;
			const auto Result = FCommandService::IssueForceOrder(PC->GetPlayerState<ACommandPlayerState>(), Force.Get(), EForceVerb::Attack, Away);
			if (!Result.IsAccepted())
			{
				Force.Reset();
				return false;
			}
			CheckRoutes(*State);
			Test->TestTrue(TEXT("Queue accepts return leg"), FCommandService::IssueForceOrder(PC->GetPlayerState<ACommandPlayerState>(), Force.Get(), EForceVerb::MoveHold, Home, nullptr, true).IsAccepted());
			Test->TestEqual(TEXT("Active plus queue publishes two paths"), Force->GetIntentRoutes().Num(), 2);
			CheckRoutes(*State);
			PC->SelectForce(Force.Get());
			bool bSelected = false;
			ForceRoutePresentation::Visit(*PC, [&](const ForceRoutePresentation::FRoute& Route) {
				if (!Route.bPreview && Route.bSelected && Route.OrderIndex == 1)
					bSelected = Route.Line.Count >= 2 && Route.Line.Points[Route.Line.Count - 1].Equals(State->GetRegionAnchor(Home));
			});
			Test->TestTrue(TEXT("Selected-force surface exposes queued return target"), bSelected);
			Source = ForceOrderGraph::SourceRegion(*Force, *State);
			return false;
		}
		if (ForceOrderGraph::SourceRegion(*Force, *State) == Source)
			return false;
		// Characters and the order driver have independent tick intervals; observe
		// the next published executor snapshot after physically crossing a border.
		const int32 Current = ForceOrderGraph::SourceRegion(*Force, *State);
		if (Force->GetIntentRoutes().IsEmpty() || Force->GetIntentRoutes()[0].Regions.IsEmpty()
			|| Force->GetIntentRoutes()[0].Regions[0] != Current)
			return false;
		CheckRoutes(*State);
		Test->TestTrue(TEXT("MoveHold replacement accepted mid-march"), FCommandService::IssueForceOrder(PC->GetPlayerState<ACommandPlayerState>(), Force.Get(), EForceVerb::MoveHold, Home).IsAccepted());
		Test->TestEqual(TEXT("Replacement removes stale queue geometry"), Force->GetIntentRoutes().Num(), 1);
		CheckRoutes(*State);
		Test->TestTrue(TEXT("Manual retreat accepted mid-route"), FCommandService::IssueForceOrder(PC->GetPlayerState<ACommandPlayerState>(), Force.Get(), EForceVerb::Retreat).IsAccepted());
		CheckRoutes(*State);
		Test->TestTrue(TEXT("Hostile structure Attack accepted"), FCommandService::IssueForceOrder(PC->GetPlayerState<ACommandPlayerState>(), Force.Get(), EForceVerb::Attack, INDEX_NONE, State->EnemyHeadquarters).IsAccepted());
		bool bStructureHighlight = false;
		ForceRoutePresentation::Visit(*PC, [&](const ForceRoutePresentation::FRoute& Route) {
			if (!Route.bPreview && Route.bSelected && Route.OrderIndex == 0 && Route.Line.Count > 0)
				bStructureHighlight = Route.Line.Points[Route.Line.Count - 1].Equals(State->EnemyHeadquarters->GetActorLocation());
		});
		Test->TestTrue(TEXT("Structure target highlight is the structure, not its region anchor"), bStructureHighlight);
		Test->AddInfo(TEXT("Published Attack/MoveHold route follows actual executor waypoints through source progression, queue replacement and structure targeting."));
		return true;
	}
private:
	void CheckRoutes(const ACommandGameState& State)
	{
		const auto& Routes = Force->GetIntentRoutes();
		if (!Test->TestFalse(TEXT("Active route is published"), Routes.IsEmpty()))
			return;
		int32 Start = ForceOrderGraph::SourceRegion(*Force, State);
		for (const FForceRoute& Route : Routes)
		{
			if (!Test->TestFalse(TEXT("Each published leg has region endpoints"), Route.Regions.IsEmpty()))
				return;
			Test->TestEqual(TEXT("Route starts at physical source or preceding queue endpoint"), Route.Regions[0], Start);
			const int32 Target = Route.Regions.Last();
			Start = Target;
		}
		const FForceRoute& Active = Routes[0];
		Test->TestTrue(TEXT("Executor waypoint is source capture or first published travel hop"),
			Force->WaypointRegionIndex == Active.Regions[0] || (Active.Regions.Num() > 1 && Force->WaypointRegionIndex == Active.Regions[1]));
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<AArmyGroup> Force;
	double Started;
	int32 Home = INDEX_NONE, Away = INDEX_NONE, Source = INDEX_NONE;
};

bool FRouteIntentWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FRouteIntentScenario(this));
	return true;
}
#endif
