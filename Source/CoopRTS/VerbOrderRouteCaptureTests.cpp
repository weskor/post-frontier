#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderRouteCaptureTests
{
using namespace VerbOrderTests;

// A march whose navmesh path crosses neutral ground that is not on its graph route
// walks on without capturing it.
class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	// Whether a unit standing at Point has a complete navmesh path to Goal that never leaves the
	// off-route region, the intermediate and home, so the walk does not run through the target.
	bool WalksTo(UNavigationSystemV1& Navigation, const FVector& Point, const FVector& Goal) const
	{
		const AArmyUnit* Unit = Force->GetUnits()[0];
		const FNavAgentProperties& Agent = Unit->GetNavAgentPropertiesRef();
		const ANavigationData* Data = Navigation.GetNavDataForProps(Agent, Point);
		FNavLocation From, To;
		if (!Data || !Navigation.ProjectPointToNavigation(Point, From, FVector(35.f, 35.f, 200.f), Data)
			|| FVector::Dist2D(Point, From.Location) > 35.f
			|| !Navigation.ProjectPointToNavigation(Goal, To, FVector(75.f, 75.f, 200.f), Data))
			return false;
		FPathFindingQuery Query(nullptr, *Data, From.Location, To.Location);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Path = Navigation.FindPathSync(Agent, Query);
		if (!Path.IsSuccessful() || !Path.Path.IsValid() || Path.Path->IsPartial())
			return false;
		for (const FNavPathPoint& PathPoint : Path.Path->GetPathPoints())
		{
			const AMapRegion* Crossed = State->FindRegionAt(PathPoint.Location);
			if (!Crossed || (Crossed->RegionIndex != OffRoute && Crossed->RegionIndex != Intermediate && Crossed->RegionIndex != Home))
				return false;
		}
		return true;
	}

	// The point inside OffRoute nearest the intermediate's anchor that is clear of the region's
	// capture radius, has room for the force's line of members across Y and from which the navmesh
	// reaches the intermediate.
	bool FindStray(FVector& OutPoint) const
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GameWorld);
		const AMapRegion* Off = Region(State, OffRoute);
		const FVector Anchor = State->GetRegionAnchor(OffRoute);
		const FVector Route = State->GetRegionAnchor(Intermediate);
		bool bFound = false;
		for (int32 Step = 0; Navigation && Step < 8; ++Step)
			for (float Distance = ACapturePoint::CaptureRadius + 150.f; Distance < 2500.f; Distance += 100.f)
			{
				const FVector Direction = FVector::XAxisVector.RotateAngleAxis(Step * 45.f, FVector::ZAxisVector);
				const FVector Point = Anchor + Direction * Distance;
				if (bFound && FVector::DistSquared2D(Point, Route) >= FVector::DistSquared2D(OutPoint, Route))
					continue;
				bool bRoom = true;
				for (float Across = -150.f; Across <= 150.f; Across += 150.f)
					bRoom &= Off->Contains(Point + FVector(0.f, Across, 0.f)) && WalksTo(*Navigation, Point + FVector(0.f, Across, 0.f), Route);
				if (bRoom)
				{
					OutPoint = Point + FVector(0.f, 0.f, 100.f);
					bFound = true;
				}
			}
		return bFound;
	}

	// A neutral capturable region beside the route but not on it, and a spot in it the force can be
	// carried to and still walk back to the intermediate without crossing the target.
	bool FindClip(FVector& OutStray)
	{
		for (const AMapRegion* Candidate : State->Regions)
		{
			if (!IsValid(Candidate))
				continue;
			const int32 Index = Candidate->RegionIndex;
			if (Index == Home || Index == Intermediate || Index == Target || Index == EnemyHome || Candidate->HomeTeam >= 0
				|| !IsValid(Candidate->Anchor) || State->GetRegionController(Index) != -1
				|| !(Candidate->Neighbours.Contains(Home) || Candidate->Neighbours.Contains(Intermediate)))
				continue;
			OffRoute = Index;
			if (FindStray(OutStray))
				return true;
		}
		OffRoute = INDEX_NONE;
		return false;
	}

	bool RunScenario() override
	{
		if (Stage == 0)
		{
			FVector Stray;
			if (!Check(FindClip(Stray), TEXT("Map has a neutral capturable region beside the route that the force can be carried into and walk back from"))
				|| !Issue(EForceVerb::MoveHold, Target)
				|| !Check(Force->WaypointRegionIndex == Intermediate, TEXT("Two-step MoveHold begins at the adjacent neutral waypoint")))
				return true;
			// The navmesh path has carried the force across the off-route region's polygon: stand it
			// there with its old path dropped, as the executor sees a force mid-march.
			const TArray<AArmyUnit*>& Units = Force->GetUnits();
			for (int32 Index = 0; Index < Units.Num(); ++Index)
			{
				if (AAIController* AI = Cast<AAIController>(Units[Index]->GetController()))
					AI->StopMovement();
				Units[Index]->SetActorLocation(Stray + FVector(0.f, (Index - Units.Num() / 2) * 40.f, 0.f), false, nullptr,
					ETeleportType::TeleportPhysics);
			}
			const AMapRegion* Underfoot = State->FindRegionAt(Force->GetCenter());
			if (!Check(Underfoot && Underfoot->RegionIndex == OffRoute, TEXT("Force stands in the off-route region")))
				return true;
			TickForce();
			if (!Check(Force->WaypointRegionIndex == Intermediate && Force->TargetRegionIndex == Target,
					TEXT("Crossing an off-route region keeps the route waypoint instead of capturing it")))
				return true;
			SetStage(1);
		}
		if (!Check(Force->WaypointRegionIndex != OffRoute && State->GetRegionController(OffRoute) == -1,
				TEXT("The march never targets or captures the off-route region")))
			return true;
		if (Stage == 1 && Holding(Target))
			return Check(State->GetRegionController(Intermediate) == 0 && State->GetRegionController(OffRoute) == -1,
				*FString::Printf(TEXT("The force walks on, secures its route and leaves the off-route region neutral "
									  "(home=%d via=%d target=%d off=%d controllers via=%d off=%d)"),
					Home, Intermediate, Target, OffRoute, State->GetRegionController(Intermediate),
					State->GetRegionController(OffRoute)));
		return false;
	}

	int32 OffRoute = INDEX_NONE;
};
}

VERB_WORLD_TEST(FVerbRouteCaptureTest, "RouteCapture", RouteCapture)

#endif
