#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "VerbOrderFixture.h"

namespace VerbOrderRouteCaptureClipTests
{
using namespace VerbOrderTests;

// A real order between two adjacent neutral regions whose navmesh path crosses a third neutral
// region that is not on the route: the force walks through it without capturing it.
class FScenario : public FScenarioBase
{
public:
	FScenario(FAutomationTestBase* InTest)
		: FScenarioBase(InTest, EScenario::MoveHold) {}

private:
	bool Capturable(const AMapRegion* Candidate) const
	{
		return IsValid(Candidate) && Candidate->HomeTeam < 0 && IsValid(Candidate->Anchor)
			&& State->GetRegionController(Candidate->RegionIndex) == -1;
	}

	// Whether the navmesh path between the two anchors enters Crossed, keeping clear of its
	// capture point so the crossing cannot capture it by presence.
	bool PathClips(UNavigationSystemV1& Navigation, int32 From, int32 To, int32 Crossed) const
	{
		const FNavAgentProperties& Agent = Force->GetUnits()[0]->GetNavAgentPropertiesRef();
		const ANavigationData* Data = Navigation.GetNavDataForProps(Agent, State->GetRegionAnchor(From));
		FNavLocation PathStart, PathEnd;
		if (!Data || !Navigation.ProjectPointToNavigation(State->GetRegionAnchor(From), PathStart, FVector(75.f, 75.f, 200.f), Data)
			|| !Navigation.ProjectPointToNavigation(State->GetRegionAnchor(To), PathEnd, FVector(75.f, 75.f, 200.f), Data))
			return false;
		FPathFindingQuery Query(nullptr, *Data, PathStart.Location, PathEnd.Location);
		Query.SetAllowPartialPaths(false);
		const FPathFindingResult Result = Navigation.FindPathSync(Agent, Query);
		if (!Result.IsSuccessful() || !Result.Path.IsValid() || Result.Path->IsPartial())
			return false;
		const TArray<FNavPathPoint>& Points = Result.Path->GetPathPoints();
		const FVector CrossedAnchor = State->GetRegionAnchor(Crossed);
		bool bEntered = false;
		for (int32 Index = 1; Index < Points.Num(); ++Index)
		{
			const FVector Segment = Points[Index].Location - Points[Index - 1].Location;
			const int32 Samples = FMath::CeilToInt(Segment.Size2D() / 50.f);
			for (int32 Sample = 0; Sample <= Samples; ++Sample)
			{
				const FVector Point = Points[Index - 1].Location + Segment * (Samples ? float(Sample) / Samples : 0.f);
				const AMapRegion* At = State->FindRegionAt(Point);
				if (!At || At->RegionIndex != Crossed)
					continue;
				if (FVector::Dist2D(Point, CrossedAnchor) < ACapturePoint::CaptureRadius + 150.f)
					return false;
				bEntered = true;
			}
		}
		return bEntered;
	}

	bool FindClip()
	{
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(GameWorld);
		for (const AMapRegion* From : State->Regions)
			for (int32 ToIndex : From->Neighbours)
			{
				const AMapRegion* To = Region(State, ToIndex);
				if (!Navigation || !Capturable(From) || !Capturable(To))
					continue;
				for (const AMapRegion* Via : State->Regions)
					if (Via != From && Via != To && Capturable(Via) && PathClips(*Navigation, From->RegionIndex, ToIndex, Via->RegionIndex))
					{
						Start = From->RegionIndex;
						End = ToIndex;
						OffRoute = Via->RegionIndex;
						return true;
					}
			}
		return false;
	}

	bool RunScenario() override
	{
		if (Stage == 0)
		{
			if (!Check(FindClip(), TEXT("Map has two adjacent neutral regions whose navmesh path clips a third neutral region")))
				return true;
			// Start the force at the first region's anchor, then give a real order to the adjacent one.
			const FVector Anchor = State->GetRegionAnchor(Start) + FVector(0.f, 0.f, 100.f);
			const TArray<AArmyUnit*>& Units = Force->GetUnits();
			for (int32 Index = 0; Index < Units.Num(); ++Index)
			{
				if (AAIController* AI = Cast<AAIController>(Units[Index]->GetController()))
					AI->StopMovement();
				Units[Index]->SetActorLocation(Anchor + FVector(0.f, (Index - Units.Num() / 2) * 40.f, 0.f), false, nullptr,
					ETeleportType::TeleportPhysics);
			}
			const AMapRegion* Underfoot = State->FindRegionAt(Force->GetCenter());
			if (!Check(Underfoot && Underfoot->RegionIndex == Start, TEXT("Force starts in the first region"))
				|| !Issue(EForceVerb::MoveHold, End))
				return true;
			SetStage(1);
		}
		bCrossed |= Region(State, OffRoute)->Contains(Force->GetCenter());
		if (!Check(Force->WaypointRegionIndex != OffRoute && State->GetRegionController(OffRoute) == -1,
				*FString::Printf(TEXT("The march never targets or captures the clipped region (start=%d end=%d clipped=%d)"),
					Start, End, OffRoute)))
			return true;
		if (Stage == 1 && Holding(End))
			return Check(bCrossed && State->GetRegionController(Start) == 0,
				*FString::Printf(TEXT("The force physically crossed the clipped region and secured both route regions "
									  "(start=%d end=%d clipped=%d crossed=%d)"),
					Start, End, OffRoute, bCrossed ? 1 : 0));
		return false;
	}

	int32 Start = INDEX_NONE, End = INDEX_NONE, OffRoute = INDEX_NONE;
	bool bCrossed = false;
};
}

VERB_WORLD_TEST(FVerbRouteCaptureClipTest, "RouteCaptureClip", RouteCaptureClip)

#endif
