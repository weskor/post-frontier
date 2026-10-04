#pragma once

#include "CoreMinimal.h"
#include "Content/BuildingDefinition.h"
#include "Content/UnitDefinition.h"
#include "Engine/DataAsset.h"
#include "MatchContent.generated.h"

// The match's unit and building catalogue. Actors replicate indices into these arrays,
// so order is part of the asset's contract (see Build/GenerateMatchContent.py).
UCLASS(BlueprintType)
class COOPRTS_API UMatchContent : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TArray<TObjectPtr<UArmyUnitDefinition>> Units;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Content")
	TArray<TObjectPtr<UBuildingDefinition>> Buildings;

	// nullptr when the index is out of range or the slot is empty.
	const UArmyUnitDefinition* Unit(int32 Index) const { return Units.IsValidIndex(Index) ? Units[Index].Get() : nullptr; }
	const UBuildingDefinition* Building(int32 Index) const { return Buildings.IsValidIndex(Index) ? Buildings[Index].Get() : nullptr; }
	// -1 when absent.
	int32 IndexOf(const UArmyUnitDefinition* Definition) const;
	int32 IndexOf(const UBuildingDefinition* Definition) const;
	int32 UnitIndexOf(FName Id) const;
	// Catalogue index of the tier-2 branch derived from the base unit at BaseIndex; -1 when it has none.
	int32 BranchIndexOf(int32 BaseIndex) const;
	int32 BuildingIndexOf(FName Id) const;
	const UArmyUnitDefinition* FindUnit(FName Id) const { return Unit(UnitIndexOf(Id)); }
	const UBuildingDefinition* FindBuilding(FName Id) const { return Building(BuildingIndexOf(Id)); }
	// Migration-only: first base (non-branch) definition matching a legacy enum; -1 when absent.
	int32 UnitIndexForRole(EUnitRole Role) const;
	int32 BuildingIndexForKind(EBuildingKind Kind) const;
};
