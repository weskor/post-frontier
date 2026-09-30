#pragma once

#include "CoreMinimal.h"

// Distances are inclusive and use XY only, matching actor placement previews.
struct FPlacementSector
{
	FVector Position;
	int32 ControllingTeam;
	bool bEnemyPresent, bFriendlyPresent, bEstablishedForTeam, bHasOutpost;
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
	Contested, OutpostExists, CaptureRequired, TerritoryRequired, EnemyTroopsTooClose,
	BuildingOverlap, HeadquartersTooClose
};

struct FPlacementInput
{
	int32 Team;
	FVector Position, HomePosition, HostilePosition;
	float FootprintRadius, TerritoryRadius;
	bool bInsidePlacementBounds, bHeadquartersAvailable, bSectorBuilding;
	TConstArrayView<FPlacementSector> Sectors;
	TConstArrayView<FPlacementBuilding> Buildings;
	TConstArrayView<FVector> EnemyTroops;
};

struct FPlacementDecision
{
	EPlacementVerdict Verdict;
	int32 TargetSectorIndex = INDEX_NONE;
};

namespace PlacementPolicy
{
	constexpr float HostileHeadquartersClearance = 1000.f;
	constexpr float HomeTerritoryRadius = 900.f;
	constexpr float HeadquartersClearance = 210.f;
	constexpr float EnemyTroopClearance = 330.f;
	constexpr float BuildingClearance = 55.f;
	int32 SelectTargetSector(int32 Team, const FVector& Position, float TerritoryRadius,
		float FootprintRadius, TConstArrayView<FPlacementSector> Sectors);
	FPlacementDecision Evaluate(const FPlacementInput& In);
}
