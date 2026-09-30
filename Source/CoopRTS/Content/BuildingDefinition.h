#pragma once

#include "CoreMinimal.h"
#include "ConstructionTypes.h"
#include "Engine/DataAsset.h"
#include "BuildingDefinition.generated.h"

class UStaticMesh;

UCLASS(BlueprintType)
class COOPRTS_API UBuildingDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	// Stable identity for scripts and tests, e.g. "barracks".
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FLinearColor Accent = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Construction")
	int32 BuildCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Construction")
	int32 MaxHealth = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Construction")
	float BuildDuration = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Construction")
	float FootprintRadius = 0.f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bProducesForces = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bEstablishesSector = false;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Capabilities")
	bool bOffersResearch = false;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> HumanMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MachineMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> ConstructionMesh;
	// Producer locked-type variants, indexed by UMatchContent unit order; empty entries keep the neutral mesh.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TArray<TSoftObjectPtr<UStaticMesh>> HumanRoleMeshes;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TArray<TSoftObjectPtr<UStaticMesh>> MachineRoleMeshes;

	// Migration-only: EBuildingKind is derived from capabilities for callers that still branch on it.
	EBuildingKind GetKind() const
	{
		return bProducesForces ? EBuildingKind::Barracks : bEstablishesSector ? EBuildingKind::Outpost : EBuildingKind::Workshop;
	}
};
