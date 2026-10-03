#pragma once

#include "CoreMinimal.h"
#include "ArmyUnit.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
#include "Content/BuildingDefinition.h"
#include "Rules/ProductionPolicy.h"
#include "GameFramework/Actor.h"
#include "CommandBuilding.generated.h"
class ADepositSite;
class AArmyGroup;
class AMapRegion;
class UBoxComponent;
class UStaticMeshComponent;
class UStaticMesh;
class UMaterialInterface;

USTRUCT()
struct FCommandBuildingTerminalSnapshot
{
	GENERATED_BODY()

	UPROPERTY()
	bool bCancelled = false;
	UPROPERTY()
	int32 Health = 0;
	UPROPERTY()
	float ConstructionProgress = 0.f;
	UPROPERTY()
	int32 TeamIndex = 0;
};

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
	EArmorClass GetArmorClass() const { return EArmorClass::Structure; }
	int32 MaxHealth() const;
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	void NotifyPlacementCommitted();
	bool TrySpend(int32 Cost);

	// Production implementation lives in CommandBuildingProduction.cpp.
	int32 GetProductionCost() const;
	float GetProductionDuration() const;
	EProductionState GetProductionState() const;
	void GetForceCounts(int32& OutJoined, int32& OutTravelling) const;
	void TickProduction(float DeltaSeconds);
	void InitializeRallyPoint();

	// Index into Content->Buildings; the identity spawners set. Kind is derived from it on the server.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Building")
	int32 BuildingIndex = -1;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Building")
	EBuildingKind Kind = EBuildingKind::Barracks;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Building")
	int32 TeamIndex = 0;
	// Reserved deposit; completed extractors pay only their builder and never lock region capture.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	TObjectPtr<ADepositSite> Deposit;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Building")
	TObjectPtr<ACommandPlayerState> OwningPlayerState;
	// Lowest free positive producer number for this owner; zero for non-producers.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Building")
	int32 ForceNumber = 0;
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
	int32 RallyRegionIndex = INDEX_NONE;

private:
	friend class FCommandService;
	bool ApplyCancellation();
	bool ApplyResearch(EArmyDoctrine Choice);
	bool ApplyProduction(int32 UnitIndex, bool bEnabled);

	bool FindProductionExit(FVector& OutLocation, int32& Cursor) const;
	float ProductionCheckAccumulator = 0.f;
	UPROPERTY(ReplicatedUsing = OnRep_PlacementCommitted)
	double PlacementCommittedServerTime = -1.;
	UPROPERTY(ReplicatedUsing = OnRep_DeploymentCount)
	uint32 DeploymentCount = 0;
	UPROPERTY(ReplicatedUsing = OnRep_ResearchCount)
	uint32 ResearchCount = 0;
	bool bAudioStateInitialized = false;
	bool bPlacementAudioObserved = false;
	bool bConstructionAudioRunning = false;
	bool bTerminalAudioHandled = false;
	bool bTerminalCancelled = false;
	bool bAudioWasComplete = false;
	int32 AudioPreviousHealth = 0;
	uint32 AudioDeploymentCount = 0;
	uint32 AudioResearchCount = 0;
	void NotifyAudioState();
	void NotifyTerminalAudio();
	void StopConstructionAudio();
	void ReleaseDeposit();
	UFUNCTION()
	void OnRep_PlacementCommitted();
	UFUNCTION()
	void OnRep_DeploymentCount();
	UFUNCTION()
	void OnRep_ResearchCount();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastTerminalState(const FCommandBuildingTerminalSnapshot& Snapshot);
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
