#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ConstructionTypes.h"
#include "Rules/HoldPolicy.h"
#include "ArmyGroup.generated.h"

class AArmyUnit;
class ACommandBuilding;
enum class EArmyDoctrine : uint8;
class ACommandPlayerState;

struct FArmyGroupSpawn
{
	int32 TeamIndex = 0;
	ACommandPlayerState* OwningPlayerState = nullptr;
	int32 ArmyIndex = 0;
	ACommandBuilding* ProductionBuilding = nullptr;
	FVector HomeLocation = FVector::ZeroVector;
};

UENUM(BlueprintType)
enum class EArmyOrder : uint8
{
	Hold,
	Move,
	Attack,
	Retreat
};

UENUM(BlueprintType)
enum class EHoldThreatKind : uint8
{
	Intrusion,
	Force,
	Building,
	Headquarters
};

UCLASS()
class COOPRTS_API AArmyGroup : public AActor
{
	GENERATED_BODY()

public:
	AArmyGroup();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	void Initialize(const FArmyGroupSpawn& Spawn);
	void OnMemberDied(AArmyUnit* Unit);
	void DetachProducer();
	void RollbackLastReinforcement();
	void SetAssemblyLocation(const FVector& Location);
	int32 GetTeamIndex() const { return TeamIndex; }
	ACommandPlayerState* GetOwningPlayerState() const { return OwningPlayerState.Get(); }
	int32 GetArmyIndex() const { return ArmyIndex; }
	ACommandBuilding* GetProductionBuilding() const { return ProductionBuilding.Get(); }
	// Retained after producer death; living survivors reserve the number against reuse.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	int32 ForceNumber = 0;
	const FVector& GetHomeLocation() const { return HomeLocation; }
	bool IsOpposingArmy() const { return bOpposingArmy; }
	const TArray<TObjectPtr<AArmyUnit>>& GetUnits() const { return Units; }
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	bool SpawnUnits();
	// Authority-owned encounters may create joined members without a producer.
	// Paid reinforcement continues to use SpawnReinforcement exclusively.
	AArmyUnit* SpawnMember(int32 UnitIndex, const FVector& SpawnLocation, int32 CompositionSlot);
#endif
	bool SpawnReinforcement(int32 UnitIndex, const FVector& SpawnLocation);
	void SettleMatch();
	FVector GetCenter() const;
	EArmyDoctrine GetDoctrine() const;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	EArmyOrder Order = EArmyOrder::Hold;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	FVector Destination = FVector::ZeroVector;

	UPROPERTY(Replicated)
	uint32 OrderSerial = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	TObjectPtr<AActor> AttackTarget;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	EFrontOrder FrontOrder = EFrontOrder::Defend;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	FVector FrontLocation = FVector::ZeroVector;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	bool bAutomaticFront = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hold")
	int32 HoldRegionIndex = INDEX_NONE;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hold")
	int32 HoldPostIndex = INDEX_NONE;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hold")
	FVector HoldPostLocation = FVector::ZeroVector;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hold")
	bool bHoldResponding = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hold")
	TObjectPtr<AArmyUnit> HoldThreat;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hold")
	TObjectPtr<AActor> HoldThreatenedAsset;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Hold")
	EHoldThreatKind HoldThreatKind = EHoldThreatKind::Intrusion;
	bool IsHoldingRegion() const;
	bool IsHoldTargetPermitted(const AArmyUnit& Target) const;
	double GetHoldResponseStarted() const { return HoldClock.Started; }
	double GetHoldQuietSince() const { return HoldClock.QuietSince; }

	static constexpr float PursuitRadius = 1050.f;


protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	TArray<TObjectPtr<AArmyUnit>> Units;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	FVector HomeLocation = FVector::ZeroVector;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	int32 TeamIndex = 0;
	// Available to every peer; a remote controller is not necessarily present.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	TObjectPtr<ACommandPlayerState> OwningPlayerState;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	int32 ArmyIndex = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	TObjectPtr<ACommandBuilding> ProductionBuilding;
	bool bOpposingArmy = false;

private:
	friend class FCommandService;
	friend class ACommandBuilding;
	friend class ACommandGameState;
	HoldPolicy::FClock HoldClock;
	int32 HoldPostSlot = INDEX_NONE;
	void ResetHoldState();
	void UpdateHoldCombat();
	void UpdateHoldMovement(AArmyUnit& Unit, const FVector& Goal);
	bool AssignFront(EFrontOrder InOrder, const FVector& InLocation);
	bool ApplyAttack(FVector InDestination, AActor* InTarget);
	bool ApplyHold();
	bool IssueTravel(EArmyOrder NewOrder, const FVector& InDestination);
	void UpdateCombat();
	void UpdateReinforcements();
	FVector ReinforcementTarget(const AArmyUnit& Unit) const;
	float CombatAccumulator = 0.f;
	float FrontMaintenanceSeconds = 0.f;
	// Stable composition slots retain retry clocks through unit-array compaction.
	TArray<float, TInlineAllocator<6>> NextPursuitAttempts;
	bool bProducedGroup = false;
	int32 ForceCapacity = 0;
	void StopAllUnits();
	void LogOrder() const;
	FVector FormationOffset(int32 Index) const;
};
