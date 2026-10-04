#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Rules/CombatPolicy.h"
#include "UnitDefinition.generated.h"

class UStaticMesh;

// Combat/doctrine behaviour only; everything else about a unit is data on its definition.
UENUM(BlueprintType)
enum class EUnitRole : uint8
{
	Frontline,
	Ranged,
	Siege,
	Assault,
	Support,
	Unset UMETA(Hidden)
};

UCLASS(BlueprintType)
class COOPRTS_API UArmyUnitDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	// Stable identity for scripts and tests, e.g. "frontline".
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FName Id;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FText DisplayName;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Identity")
	FLinearColor Accent = FLinearColor::White;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	int32 UnitCost = 0;
	// Living members (joined plus travelling) a producer force of this unit holds.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	int32 Capacity = 0;
	// Charged once when a producer permanently locks this force type.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	int32 ConfigurationCost = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Production")
	float UnitDuration = 0.f;
	// Tier-2 branch: Id of the base unit this definition derives from (same cost, capacity and duration, changed
	// combat stats), None for a base type. A branch is bought per production building, never produced on its own.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Branch")
	FName BranchOf;
	// What the branch changes, as the production panel prints it, e.g. "+20% range".
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Branch")
	FText BranchSummary;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> HumanMesh;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Appearance")
	TSoftObjectPtr<UStaticMesh> MachineMesh;

	// No authored enum equals its CDO default, so all three serialize explicitly.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	EUnitRole Role = EUnitRole::Unset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 MaxHealth = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 AttackDamage = 0;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Range = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Interval = 0.f;
	// Multiplies this unit's damage against Structure armor, after the class bonus (Demolisher 1.5); 1 = none.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float StructureDamageMultiplier = 1.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	EArmorClass ArmorClass = EArmorClass::Unset;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	EDamageType DamageType = EDamageType::Unset;
	// Shield points above HP; 0 = no shield. Regenerates per ShieldPolicy.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 MaxShield = 0;
	// Automatic pulse (Scrambler): 0 interval = no pulse. Strips hostile shields and stuns hostile buildings in the radius.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pulse")
	float PulseInterval = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pulse")
	float PulseRadius = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Pulse")
	float PulseBuildingStunSeconds = 0.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Movement")
	float MoveSpeed = 0.f;

	bool IsBranch() const { return !BranchOf.IsNone(); }
};
