#pragma once

#include "CoreMinimal.h"

// Geometry and ownership only: policies never read actors or world state.
struct FPlacementRegion
{
	int32 RegionIndex;
	TConstArrayView<FVector2D> Polygon;
	int32 ControllingTeam;
	bool bContested;
};

struct FPlacementDeposit
{
	FVector Position;
	int32 RegionIndex, ControllingTeam;
	bool bOccupied, bContested;
};

struct FPlacementBuilding
{
	FVector Position;
	float FootprintRadius;
	bool bAlive;
};

enum class EPlacementVerdict : uint8
{
	Valid, Invalid, OutsideBounds, HeadquartersUnavailable, EnemyHeadquartersTooClose,
	Contested, TerritoryRequired, EnemyTroopsTooClose,
	BuildingOverlap, HeadquartersTooClose
};

struct FPlacementInput
{
	int32 Team;
	FVector Position, HomePosition, HostilePosition;
	float FootprintRadius;
	bool bInsidePlacementBounds, bHeadquartersAvailable;
	TConstArrayView<FPlacementRegion> Regions;
	TConstArrayView<FPlacementBuilding> Buildings;
	TConstArrayView<FVector> EnemyTroops;
};

struct FPlacementDecision
{
	EPlacementVerdict Verdict;
	int32 RegionIndex = INDEX_NONE;
};

namespace PlacementPolicy
{
	constexpr float BuildGridCellSize = 50.f;
	int32 FootprintCells(float HalfExtent);
	// Snap XY to whole-cell footprints; preserve terrain height. Ties choose positive XY.
	FVector SnapToBuildGrid(const FVector& Position, float HalfExtent);
	constexpr float HostileHeadquartersClearance = 1000.f;
	constexpr float DepositSnapRadius = 300.f;
	constexpr float HeadquartersClearance = 210.f;
	constexpr float EnemyTroopClearance = 330.f;
	constexpr float BuildingClearance = 55.f;
	// Polygon boundaries are included, for either winding order and concave polygons.
	bool ContainsPoint(TConstArrayView<FVector2D> Polygon, const FVector2D& Point);
	bool ContainsFootprint(TConstArrayView<FVector2D> Polygon, const FVector& Position, float HalfExtent);
	int32 RegionController(bool bMain, int32 HomeTeam, bool bHomeHeadquartersAlive, int32 AnchorController);
	// Ties retain the first deposit in the stable region/XY order; occupancy excludes construction too.
	int32 SelectFreeDeposit(int32 Team, const FVector& RequestedLocation, TConstArrayView<FPlacementDeposit> Deposits);
	// Bounds and territory only; excludes HQ clearance, troops and building overlap.
	FPlacementDecision EvaluateTerritory(const FPlacementInput& In);
	FPlacementDecision Evaluate(const FPlacementInput& In);
}
