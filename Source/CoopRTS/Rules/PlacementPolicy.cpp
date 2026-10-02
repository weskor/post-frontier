#include "PlacementPolicy.h"

namespace
{
bool Near(const FVector& Position, const FVector& Center, float Range)
{
	return FVector::DistSquared2D(Position, Center) <= FMath::Square(Range);
}
}

int32 PlacementPolicy::FootprintCells(float HalfExtent)
{
	return FMath::CeilToInt(2.f * HalfExtent / BuildGridCellSize);
}

FVector PlacementPolicy::SnapToBuildGrid(const FVector& Position, float HalfExtent)
{
	const double Offset = FootprintCells(HalfExtent) % 2 ? BuildGridCellSize * .5 : 0.;
	return FVector(
		FMath::FloorToDouble((Position.X - Offset) / BuildGridCellSize + .5) * BuildGridCellSize + Offset,
		FMath::FloorToDouble((Position.Y - Offset) / BuildGridCellSize + .5) * BuildGridCellSize + Offset,
		Position.Z);
}

bool PlacementPolicy::ContainsPoint(TConstArrayView<FVector2D> Polygon, const FVector2D& Point)
{
	if (Polygon.Num() < 3)
		return false;
	bool bInside = false;
	for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
	{
		const FVector2D& A = Polygon[Previous];
		const FVector2D& B = Polygon[Index];
		const FVector2D Edge = B - A;
		const FVector2D Offset = Point - A;
		const double Cross = Edge.X * Offset.Y - Edge.Y * Offset.X;
		const double LengthSquared = Edge.SizeSquared();
		// Include a boundary with sub-micrometre tolerance, without treating zero-length edges as infinite lines.
		if (LengthSquared > 0. && FMath::Abs(Cross) <= 1.e-6 * FMath::Sqrt(LengthSquared)
			&& FVector2D::DotProduct(Offset, Edge) >= 0.
			&& FVector2D::DotProduct(Offset, Edge) <= LengthSquared)
			return true;
		if ((A.Y > Point.Y) != (B.Y > Point.Y)
			&& Point.X < A.X + (Point.Y - A.Y) * Edge.X / Edge.Y)
			bInside = !bInside;
	}
	return bInside;
}

bool PlacementPolicy::ContainsFootprint(TConstArrayView<FVector2D> Polygon, const FVector& Position, float HalfExtent)
{
	const FVector2D Center(Position.X, Position.Y);
	return ContainsPoint(Polygon, Center)
		&& ContainsPoint(Polygon, Center + FVector2D(-HalfExtent, -HalfExtent))
		&& ContainsPoint(Polygon, Center + FVector2D(-HalfExtent, HalfExtent))
		&& ContainsPoint(Polygon, Center + FVector2D(HalfExtent, -HalfExtent))
		&& ContainsPoint(Polygon, Center + FVector2D(HalfExtent, HalfExtent));
}

int32 PlacementPolicy::RegionController(bool bMain, int32 HomeTeam, bool bHomeHeadquartersAlive, int32 AnchorController)
{
	if (bMain)
		return bHomeHeadquartersAlive && (HomeTeam == 0 || HomeTeam == 5) ? HomeTeam : -1;
	return AnchorController == 0 || AnchorController == 5 ? AnchorController : -1;
}

int32 PlacementPolicy::SelectFreeDeposit(int32 Team, const FVector& RequestedLocation, TConstArrayView<FPlacementDeposit> Deposits)
{
	if (Team != 0 && Team != 5)
		return INDEX_NONE;
	int32 Target = INDEX_NONE;
	double Nearest = FMath::Square(DepositSnapRadius);
	for (int32 Index = 0; Index < Deposits.Num(); ++Index)
	{
		if (Deposits[Index].bOccupied || Deposits[Index].bContested || Deposits[Index].ControllingTeam != Team)
			continue;
		const double Distance = FVector::DistSquared2D(RequestedLocation, Deposits[Index].Position);
		if (Distance <= Nearest && (Target == INDEX_NONE || Distance < Nearest))
		{
			Target = Index;
			Nearest = Distance;
		}
	}
	return Target;
}

FPlacementDecision PlacementPolicy::EvaluateTerritory(const FPlacementInput& In)
{
	if (In.Team != 0 && In.Team != 5)
		return { EPlacementVerdict::Invalid };
	if (!In.bInsidePlacementBounds)
		return { EPlacementVerdict::OutsideBounds };
	if (!In.bHeadquartersAvailable)
		return { EPlacementVerdict::HeadquartersUnavailable };
	int32 ContestedRegion = INDEX_NONE;
	for (const FPlacementRegion& Region : In.Regions)
	{
		if (Region.ControllingTeam != In.Team
			|| !ContainsFootprint(Region.Polygon, In.Position, In.FootprintRadius))
			continue;
		if (!Region.bContested)
			return { EPlacementVerdict::Valid, Region.RegionIndex };
		ContestedRegion = Region.RegionIndex;
	}
	return ContestedRegion != INDEX_NONE
		? FPlacementDecision{ EPlacementVerdict::Contested, ContestedRegion }
		: FPlacementDecision{ EPlacementVerdict::TerritoryRequired };
}

FPlacementDecision PlacementPolicy::Evaluate(const FPlacementInput& In)
{
	const FPlacementDecision Territory = EvaluateTerritory(In);
	if (Territory.Verdict == EPlacementVerdict::Invalid || Territory.Verdict == EPlacementVerdict::OutsideBounds
		|| Territory.Verdict == EPlacementVerdict::HeadquartersUnavailable)
		return Territory;
	const float Radius = In.FootprintRadius;
	if (Near(In.Position, In.HostilePosition, HostileHeadquartersClearance + Radius))
		return { EPlacementVerdict::EnemyHeadquartersTooClose };
	if (Territory.Verdict != EPlacementVerdict::Valid)
		return Territory;
	const int32 Target = Territory.RegionIndex;
	for (const FVector& Troop : In.EnemyTroops)
		if (Near(In.Position, Troop, Radius + EnemyTroopClearance))
			return { EPlacementVerdict::EnemyTroopsTooClose, Target };
	for (const FPlacementBuilding& Building : In.Buildings)
		if (Building.bAlive && Near(In.Position, Building.Position, Radius + Building.FootprintRadius + BuildingClearance))
			return { EPlacementVerdict::BuildingOverlap, Target };
	if (Near(In.Position, In.HomePosition, Radius + HeadquartersClearance) || Near(In.Position, In.HostilePosition, Radius + HeadquartersClearance))
		return { EPlacementVerdict::HeadquartersTooClose, Target };
	return { EPlacementVerdict::Valid, Target };
}
