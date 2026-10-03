#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

namespace HoldAlarmFixture
{
bool FScenario::Project(const FVector& Point, FVector& Ground) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->GetWorld());
	FNavLocation Result;
	if (!Nav || !AArenaBounds::IsTravelLocation(State->GetWorld(), Point)
		|| !Nav->ProjectPointToNavigation(Point, Result, FVector(35., 35., 300.))
		|| FVector::Dist2D(Point, Result.Location) > 35.)
		return false;
	Ground = Result.Location;
	return true;
}

bool FScenario::Reachable(const FVector& From, const FVector& To) const
{
	UNavigationSystemV1* Nav = FNavigationSystem::GetCurrent<UNavigationSystemV1>(State->GetWorld());
	const FNavAgentProperties& Agent = GetDefault<AArmyUnit>()->GetNavAgentPropertiesRef();
	const ANavigationData* Data = Nav ? Nav->GetNavDataForProps(Agent, From) : nullptr;
	if (!Data)
		return false;
	FPathFindingQuery Query(nullptr, *Data, From, To);
	Query.SetAllowPartialPaths(false);
	const FPathFindingResult Result = Nav->FindPathSync(Agent, Query);
	return Result.IsSuccessful() && Result.Path.IsValid() && !Result.Path->IsPartial();
}

bool FScenario::ClearFormation(const FVector& Center) const
{
	for (int32 Slot = 0; Slot < 6; ++Slot)
	{
		FVector Ground;
		if (!Project(Center + FVector((1 - Slot / 2) * 220., (Slot % 2 ? 1. : -1.) * 140., 0.), Ground)
			|| !Reachable(Center, Ground))
			return false;
	}
	return true;
}

bool FScenario::FindGeometry()
{
	const int32 Team = Case == ECase::Jev ? 5 : 0;
	const UArmyUnitDefinition* Definition = State->Content->Unit(UnitIndex);
	const double Range = Definition->Range;
	const FVector FriendlyHQ = (Team == 0 ? State->FriendlyHeadquarters : State->EnemyHeadquarters)->GetActorLocation();
	const FVector HostileHQ = (Team == 0 ? State->EnemyHeadquarters : State->FriendlyHeadquarters)->GetActorLocation();
	for (AMapRegion* Candidate : State->Regions)
	{
		if (!IsValid(Candidate) || (Candidate->HomeTeam >= 0 && Candidate->HomeTeam != Team) || Candidate->GetDefendPosts().Num() < 2)
			continue;
		FVector Anchor;
		if (!Project(State->GetRegionAnchor(Candidate->RegionIndex), Anchor) || !ClearFormation(Anchor))
			continue;
		if (FVector::DistSquared2D(Anchor, FriendlyHQ) >= FVector::DistSquared2D(Anchor, HostileHQ))
			continue;
		bool bPostsClear = true;
		for (const FVector& Post : Candidate->GetDefendPosts())
		{
			FVector Ground;
			bPostsClear &= Project(Post, Ground) && Reachable(Anchor, Ground);
		}
		if (!bPostsClear)
			continue;
		for (int32 Edge = 0; Edge < Candidate->Polygon.Num(); ++Edge)
		{
			const FVector2D A = Candidate->Polygon[Edge];
			const FVector2D B = Candidate->Polygon[(Edge + 1) % Candidate->Polygon.Num()];
			for (double Fraction : { .25, .5, .75 })
			{
				if (TryGeometry(Candidate, Anchor, Range, A, B, Fraction))
					return true;
			}
		}
	}
	return false;
}

bool FScenario::TryGeometry(AMapRegion* Candidate, const FVector& Anchor, double Range, const FVector2D& A, const FVector2D& B, double Fraction)
{
	const FVector2D XY = FMath::Lerp(A, B, Fraction);
	FVector EdgePoint(XY.X, XY.Y, Anchor.Z);
	FVector Inward(-(B.Y - A.Y), B.X - A.X, 0.);
	Inward.Normalize();
	if (!Candidate->Contains(EdgePoint + Inward * 100.))
		Inward *= -1.;
	FVector Inside, EdgeOutside, Remote;
	if (!Project(EdgePoint + Inward * FMath::Max(250., Range * .35), Inside)
		|| !Project(EdgePoint - Inward * Range * .3, EdgeOutside)
		|| !Project(EdgePoint - Inward * (Range * 2. + 350.), Remote)
		|| !Candidate->Contains(Inside) || Candidate->Contains(EdgeOutside) || Candidate->Contains(Remote)
		|| FVector::Dist2D(Inside, Anchor) <= 1400. || !Reachable(Anchor, Inside)
		|| (Case == ECase::BuildingEdge && !Reachable(Remote, Anchor)))
		return false;
	FVector ThreatPoint;
	const FVector Tangent = FVector(B.X - A.X, B.Y - A.Y, 0.).GetSafeNormal();
	if (!Project(Inside + Tangent * 350., ThreatPoint) || !Candidate->Contains(ThreatPoint)
		|| !Reachable(Anchor, ThreatPoint) || !ClearFormation(ThreatPoint))
		return false;
	bool bFarFromPosts = true;
	const FVector AlarmPoint = Case == ECase::Border ? EdgeOutside : ThreatPoint;
	for (const FVector& Post : Candidate->GetDefendPosts())
		bFarFromPosts &= FVector::Dist2D(Post, AlarmPoint) > Range + 400.;
	if (!bFarFromPosts)
		return false;
	Region = Candidate;
	BuildingLocation = Inside;
	Intrusion = ThreatPoint;
	Outside = EdgeOutside;
	FarOutside = Remote;
	Side = Tangent;
	return true;
}

}

#endif
