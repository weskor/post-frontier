#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "FormationHeadingNeck.h"

#include "AIController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "Commands/CommandService.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "MapRegion.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"

namespace
{
constexpr double SideReach = 700.;
// Skip the start (the force's own ground) and the end (the slots) of the route when looking for the neck.
constexpr int32 SkipStart = 4, SkipEnd = 2;

int32 Hops(const ACommandGameState& State, int32 From, int32 To)
{
	TMap<int32, int32> Distance;
	TArray<int32> Queue{ From };
	Distance.Add(From, 0);
	for (int32 Head = 0; Head < Queue.Num(); ++Head)
		for (const AMapRegion* Region : State.Regions)
			if (IsValid(Region) && Region->RegionIndex == Queue[Head])
				for (const int32 Next : Region->Neighbours)
					if (!Distance.Contains(Next))
					{
						Distance.Add(Next, Distance[Queue[Head]] + 1);
						Queue.Add(Next);
					}
	return Distance.Contains(To) ? Distance[To] : INDEX_NONE;
}

// Width of the walkable corridor across the route at P: the lateral navmesh rays to either side.
double CorridorWidth(UWorld& World, const FVector& P, const FVector2D& Tangent)
{
	const FVector Side(-Tangent.Y, Tangent.X, 0.);
	double Width = 0.;
	for (const double Sign : { 1., -1. })
	{
		FVector Hit;
		Width += UNavigationSystemV1::NavigationRaycast(&World, P, P + Side * Sign * SideReach, Hit) ? FVector::Dist2D(P, Hit) : SideReach;
	}
	return Width;
}

bool ReadRoute(UWorld& World, const AArmyGroup& Force, FormationHeadingNeck::FRoute& Route)
{
	const AArmyUnit* First = Force.GetUnits().IsEmpty() ? nullptr : Force.GetUnits()[0].Get();
	const AAIController* AI = First ? Cast<AAIController>(First->GetController()) : nullptr;
	const UPathFollowingComponent* Following = AI ? AI->GetPathFollowingComponent() : nullptr;
	const FNavPathSharedPtr Path = Following ? Following->GetPath() : nullptr;
	if (!Path.IsValid() || Path->GetPathPoints().Num() < 2)
		return false;
	Route.Points.Reset();
	const TArray<FNavPathPoint>& Corners = Path->GetPathPoints();
	Route.Points.Add(Corners[0].Location);
	for (int32 Index = 1; Index < Corners.Num(); ++Index)
	{
		const FVector From = Route.Points.Last(), To = Corners[Index].Location;
		for (double At = FormationHeadingNeck::SampleSpacing; At < FVector::Dist(From, To); At += FormationHeadingNeck::SampleSpacing)
			Route.Points.Add(FMath::Lerp(From, To, At / FVector::Dist(From, To)));
		Route.Points.Add(To);
	}
	Route.NeckIndex = INDEX_NONE;
	Route.NeckWidth = TNumericLimits<double>::Max();
	for (int32 Index = SkipStart; Index < Route.Points.Num() - SkipEnd; ++Index)
	{
		const FVector2D Tangent = FVector2D(Route.Points[Index + 1] - Route.Points[Index - 1]).GetSafeNormal();
		const double Width = CorridorWidth(World, Route.Points[Index], Tangent);
		if (Width < Route.NeckWidth)
		{
			Route.NeckWidth = Width;
			Route.NeckIndex = Index;
		}
	}
	return Route.NeckIndex != INDEX_NONE;
}
// Stands the force's members in a loose block on a region's anchor, so the next order plans from there.
void StandOn(AArmyGroup& Force, const FVector& Anchor)
{
	int32 Index = 0;
	for (AArmyUnit* Unit : Force.GetUnits())
	{
		Unit->SetActorLocation(Anchor + FVector((Index % 3 - 1) * 160., (Index / 3 - .5) * 160., 100.), false, nullptr, ETeleportType::TeleportPhysics);
		++Index;
	}
}
}

bool FormationHeadingNeck::ChooseNeckRoute(UWorld& World, ACommandPlayerState& Commander, AArmyGroup& Force,
	const ACommandGameState& State, FRoute& Route)
{
	bool bFound = false;
	Route.NeckWidth = TNumericLimits<double>::Max();
	for (const AMapRegion* Start : State.Regions)
	{
		if (!IsValid(Start) || (Start->RegionRole == ERegionRole::Main && Start->HomeTeam != 0))
			continue;
		StandOn(Force, State.GetRegionAnchor(Start->RegionIndex));
		for (const AMapRegion* Region : State.Regions)
		{
			if (!IsValid(Region) || Hops(State, Start->RegionIndex, Region->RegionIndex) < 2
				|| (Region->RegionRole == ERegionRole::Main && Region->HomeTeam != 0))
				continue;
			FRoute Candidate;
			Candidate.Start = Start->RegionIndex;
			Candidate.Target = Region->RegionIndex;
			if (FCommandService::IssueForceOrder(&Commander, &Force, EForceVerb::MoveHold, Region->RegionIndex).IsAccepted()
				&& ReadRoute(World, Force, Candidate) && Candidate.NeckWidth < Route.NeckWidth)
			{
				Route = Candidate;
				bFound = true;
			}
		}
	}
	if (!bFound)
		return false;
	StandOn(Force, State.GetRegionAnchor(Route.Start));
	return FCommandService::IssueForceOrder(&Commander, &Force, EForceVerb::MoveHold, Route.Target).IsAccepted();
}

void FormationHeadingNeck::BuildNeck(UWorld& World, const FRoute& Route, int32 Index, double Gap, TArray<TWeakObjectPtr<AActor>>& Walls)
{
	static const UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	const FVector Point = Route.Points[Index];
	const FVector2D Tangent = FVector2D(Route.Points[Index + 1] - Route.Points[Index - 1]).GetSafeNormal();
	const FVector Side(-Tangent.Y, Tangent.X, 0.);
	const FRotator Yaw(0., FMath::RadiansToDegrees(FMath::Atan2(Tangent.Y, Tangent.X)), 0.);
	constexpr double Thickness = 300., HalfLength = 2000., Height = 600.;
	for (const double Sign : { 1., -1. })
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Wall = World.SpawnActor<AStaticMeshActor>(Point + Side * Sign * (Gap / 2. + HalfLength) + FVector(0., 0., Height / 2.), Yaw, Parameters);
		Wall->SetMobility(EComponentMobility::Movable);
		UStaticMeshComponent* Mesh = Wall->GetStaticMeshComponent();
		Mesh->SetStaticMesh(const_cast<UStaticMesh*>(Cube));
		Mesh->SetWorldScale3D(FVector(Thickness / 100., 2. * HalfLength / 100., Height / 100.));
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetCanEverAffectNavigation(true);
		Walls.Add(Wall);
	}
}

bool FormationHeadingNeck::Replan(UWorld& World, ACommandPlayerState& Commander, AArmyGroup& Force, const ACommandGameState& State, FRoute& Route)
{
	StandOn(Force, State.GetRegionAnchor(Route.Start));
	if (!FCommandService::IssueForceOrder(&Commander, &Force, EForceVerb::MoveHold, Route.Target).IsAccepted())
		return false;
	return ReadRoute(World, Force, Route);
}

FormationHeadingNeck::FSpread FormationHeadingNeck::MeasureSpread(const AArmyGroup& Force, const FRoute& Route)
{
	FSpread Spread;
	const FVector Centre = Force.GetCenter();
	double Nearest = TNumericLimits<double>::Max();
	for (int32 Index = 1; Index + 1 < Route.Points.Num(); ++Index)
		if (FVector::DistSquared2D(Route.Points[Index], Centre) < Nearest)
		{
			Nearest = FVector::DistSquared2D(Route.Points[Index], Centre);
			Spread.Index = Index;
		}
	if (Spread.Index == INDEX_NONE)
		return Spread;
	const FVector2D Tangent = FVector2D(Route.Points[Spread.Index + 1] - Route.Points[Spread.Index - 1]).GetSafeNormal();
	const FVector2D Side(-Tangent.Y, Tangent.X);
	double MinAcross = TNumericLimits<double>::Max(), MaxAcross = TNumericLimits<double>::Lowest();
	double MinAlong = MinAcross, MaxAlong = MaxAcross;
	for (const AArmyUnit* Unit : Force.GetUnits())
	{
		const FVector2D Offset = FVector2D(Unit->GetActorLocation() - Centre);
		const double Across = FVector2D::DotProduct(Offset, Side), Along = FVector2D::DotProduct(Offset, Tangent);
		MinAcross = FMath::Min(MinAcross, Across);
		MaxAcross = FMath::Max(MaxAcross, Across);
		MinAlong = FMath::Min(MinAlong, Along);
		MaxAlong = FMath::Max(MaxAlong, Along);
	}
	Spread.Lateral = MaxAcross - MinAcross;
	Spread.Along = MaxAlong - MinAlong;
	return Spread;
}
#endif
