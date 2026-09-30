#pragma once

#include "CoreMinimal.h"
#include "ArmyUnit.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
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
	static int32 GetBuildCost(EBuildingKind InKind);
	static float GetBuildDuration(EBuildingKind InKind);
	static float GetFootprintRadius(EBuildingKind InKind);
	static int32 GetUnitCost(EUnitRole Role);
	static float GetUnitDuration(EUnitRole Role);
	static int32 GetForceCapacity(EUnitRole Role);
	static int32 GetConfigurationCost(EUnitRole Role);
	bool IsComplete() const { return ConstructionProgress >= 1.f; }
	bool IsAlive() const { return Health > 0; }
	int32 MaxHealth() const;
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	bool CancelConstruction();
	bool TryResearch(EArmyDoctrine Choice);
	bool TrySpend(int32 Cost);
	bool HasConfiguredFront() const { return bHasConfiguredFront; }

	// Production implementation lives in CommandBuildingProduction.cpp.
	bool SetProduction(EUnitRole Role, bool bEnabled);
	bool SetFront(EFrontOrder Order, const FVector& Location);
	int32 GetProductionCost() const;
	float GetProductionDuration() const;
	FString GetProductionStatus() const;
	void GetForceCounts(int32& OutJoined, int32& OutTravelling) const;
	void TickProduction(float DeltaSeconds);

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
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Production")
	EUnitRole ProductionRole = EUnitRole::Frontline;
	// Together with ProductionRole, selects the locked-role barracks mesh.
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
	// Themed meshes; empty entries (asset missing) fall back to the scaled cube.
	// Faction meshes are indexed: 0 unconfigured barracks, 1..3 barracks by EUnitRole, 4 outpost, 5 workshop.
	// Construction meshes are indexed by EBuildingKind.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> HumanMeshes;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> MachineMeshes;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> ConstructionMeshes;
	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> CubeMaterial;
	UStaticMesh* GetThemedMesh() const;
	UFUNCTION()
	void OnRep_Appearance();
};
