#include "PlacementPolicy.h"

namespace
{
	bool Near(const FVector& Position, const FVector& Center, float Range)
	{
		return FVector::DistSquared2D(Position, Center) <= FMath::Square(Range);
	}
}

int32 PlacementPolicy::SelectTargetSector(int32 Team, const FVector& Position, float TerritoryRadius,
	float FootprintRadius, TConstArrayView<FPlacementSector> Sectors)
{
	int32 Target = INDEX_NONE;
	float Nearest = TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < Sectors.Num(); ++Index)
	{
		const FPlacementSector& Sector = Sectors[Index];
		if (Sector.ControllingTeam != Team || !Near(Position, Sector.Position, TerritoryRadius - FootprintRadius)) continue;
		const float Distance = FVector::DistSquared2D(Position, Sector.Position);
		if (Distance < Nearest) { Target = Index; Nearest = Distance; }
	}
	return Target;
}

FPlacementDecision PlacementPolicy::Evaluate(const FPlacementInput& In)
{
	if (In.Team != 0 && In.Team != 5) return { EPlacementVerdict::Invalid };
	if (!In.bInsidePlacementBounds) return { EPlacementVerdict::OutsideBounds };
	if (!In.bHeadquartersAvailable) return { EPlacementVerdict::HeadquartersUnavailable };
	const float Radius = In.FootprintRadius;
	if (Near(In.Position, In.HostilePosition, HostileHeadquartersClearance + Radius)) return { EPlacementVerdict::EnemyHeadquartersTooClose };
	bool bHomeTerritory = Near(In.Position, In.HomePosition, HomeTerritoryRadius - Radius);
	for (const FPlacementSector& Sector : In.Sectors)
	{
		if (!Near(In.Position, Sector.Position, In.TerritoryRadius - Radius)) continue;
		if ((In.Team == 0 && Sector.bEnemyPresent) || (In.Team == 5 && Sector.bFriendlyPresent))
			return { EPlacementVerdict::Contested };
		if (!In.bSectorBuilding && Sector.ControllingTeam == In.Team && Sector.bEstablishedForTeam)
			bHomeTerritory = true;
	}
	const int32 Target = In.bSectorBuilding
		? SelectTargetSector(In.Team, In.Position, In.TerritoryRadius, Radius, In.Sectors) : INDEX_NONE;
	if (Target != INDEX_NONE && In.Sectors[Target].bHasOutpost) return { EPlacementVerdict::OutpostExists, Target };
	if (In.bSectorBuilding ? Target == INDEX_NONE : !bHomeTerritory)
		return { In.bSectorBuilding ? EPlacementVerdict::CaptureRequired : EPlacementVerdict::TerritoryRequired };
	for (const FVector& Troop : In.EnemyTroops)
		if (Near(In.Position, Troop, Radius + EnemyTroopClearance)) return { EPlacementVerdict::EnemyTroopsTooClose, Target };
	for (const FPlacementBuilding& Building : In.Buildings)
		if (Building.bAlive && Near(In.Position, Building.Position, Radius + Building.FootprintRadius + BuildingClearance))
			return { EPlacementVerdict::BuildingOverlap, Target };
	if (Near(In.Position, In.HomePosition, Radius + HeadquartersClearance) || Near(In.Position, In.HostilePosition, Radius + HeadquartersClearance))
		return { EPlacementVerdict::HeadquartersTooClose, Target };
	return { EPlacementVerdict::Valid, Target };
}
