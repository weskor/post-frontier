#pragma once

#include "CoreMinimal.h"
#include "ArmyUnit.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
#include "Content/BuildingDefinition.h"
#include "Rules/ProductionPolicy.h"
#include "GameFramework/Actor.h"
#include "CommandBuilding.generated.h"
class ACapturePoint;
class AArmyGroup;
class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

UCLASS()
class COOPRTS_API ACommandBuilding : public AActor
{
	GENERATED_BODY()
public:
	ACommandBuilding();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	static constexpr int32 ResearchCost = 150;
	static int32 GetBuildCost(const UBuildingDefinition& Definition) { return Definition.BuildCost; }
	static float GetBuildDuration(const UBuildingDefinition& Definition) { return Definition.BuildDuration; }
	static float GetFootprintRadius(const UBuildingDefinition& Definition) { return Definition.FootprintRadius; }
	static int32 GetUnitCost(const UArmyUnitDefinition& Definition) { return Definition.UnitCost; }
	static float GetUnitDuration(const UArmyUnitDefinition& Definition) { return Definition.UnitDuration; }
	static int32 GetForceCapacity(const UArmyUnitDefinition& Definition) { return Definition.Capacity; }
	static int32 GetConfigurationCost(const UArmyUnitDefinition& Definition) { return Definition.ConfigurationCost; }
	// Resolved through ACommandGameState::Content; nullptr until the index is assigned or content is missing.
	const UBuildingDefinition* GetDefinition() const;
	const UArmyUnitDefinition* GetProductionDefinition() const;
	bool IsProducer() const;
	bool IsComplete() const { return ConstructionProgress >= 1.f; }
	bool IsAlive() const { return Health > 0; }
	int32 MaxHealth() const;
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	bool CancelConstruction();
	bool TryResearch(EArmyDoctrine Choice);
	bool TrySpend(int32 Cost);
	bool HasConfiguredFront() const { return bHasConfiguredFront; }

	// Production implementation lives in CommandBuildingProduction.cpp.
	bool SetProduction(int32 UnitIndex, bool bEnabled);
	bool SetFront(EFrontOrder Order, const FVector& Location);
	int32 GetProductionCost() const;
	float GetProductionDuration() const;
	EProductionState GetProductionState() const;
	void GetForceCounts(int32& OutJoined, int32& OutTravelling) const;
	void TickProduction(float DeltaSeconds);

	// Index into Content->Buildings; the identity spawners set. Kind is derived from it on the server.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Building")
	int32 BuildingIndex = -1;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Building")
	EBuildingKind Kind = EBuildingKind::Barracks;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Building")
	int32 TeamIndex = 0;
	// Only the designated sector receives rights and income from this outpost.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	TObjectPtr<ACapturePoint> OutpostSite;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Building")
	TObjectPtr<ACommandPlayerState> OwningPlayerState;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Building")
	int32 Health = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Building")
	float ConstructionProgress = 0.f;
	// Index into Content->Units; ProductionRole is derived from it when production is configured.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Production")
	int32 ProductionUnitIndex = -1;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Production")
	EUnitRole ProductionRole = EUnitRole::Frontline;
	// Together with ProductionUnitIndex, selects the locked-type producer mesh.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Production")
	bool bForceConfigured = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Production")
	TObjectPtr<AArmyGroup> ForceGroup;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Production")
	bool bProductionEnabled = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Production")
	float ProductionProgressSeconds = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Production")
	EFrontOrder FrontOrder = EFrontOrder::Defend;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Production")
	FVector FrontLocation = FVector::ZeroVector;

private:
	float ProductionCheckAccumulator = 0.f;
	// Replicated so owners' HUDs distinguish an assigned front from the placement-time default location.
	UPROPERTY(Replicated)
	bool bHasConfiguredFront = false;
	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UBoxComponent> Footprint;
	UPROPERTY(VisibleAnywhere, Category = "Building")
	TObjectPtr<UStaticMeshComponent> Body;
	// Definition mesh applied by the last OnRep_Appearance; a missing asset falls back to the scaled cube.
	FSoftObjectPath AppliedMesh;
	TSoftObjectPtr<UStaticMesh> DesiredMesh() const;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CubeMaterial;
	UFUNCTION()
	void OnRep_Appearance();
};
