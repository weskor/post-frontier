#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ConstructionTypes.h"
#include "Rules/HoldPolicy.h"
#include "ForceOrders.h"
#include "ArmyGroup.generated.h"

class AArmyUnit;
class ACommandBuilding;
class ACommandGameState;
enum class EArmyDoctrine : uint8;
class ACommandPlayerState;
class AMapRegion;
class UNavigationSystemV1;

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
	friend struct FArmyMovementTestAccess;
#endif
	bool SpawnReinforcement(int32 UnitIndex, const FVector& SpawnLocation);
	void SettleMatch();
	FVector GetCenter() const;
	EArmyDoctrine GetDoctrine() const;

	void TickOrders();
	int32 GetCapacity() const;
	int32 GetAliveCount() const;
	int32 GetJoinedCount() const;
	float GetBaseMarchSpeed() const;
	float GetMarchSpeed() const;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	EForceVerb Verb = EForceVerb::MoveHold;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	int32 TargetRegionIndex = INDEX_NONE;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	TObjectPtr<AActor> TargetStructure;
	// Active order is the first entry; bounded to three by the command layer.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	TArray<FForceOrder> Orders;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	EForceStatus Status = EForceStatus::Holding;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	ERetreatThreshold RetreatThreshold = ERetreatThreshold::Percent40;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	float MarchSpeed = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	int32 WaypointRegionIndex = INDEX_NONE;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	int32 ResumeCount = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	EArmyOrder Order = EArmyOrder::Hold;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	FVector Destination = FVector::ZeroVector;

	UPROPERTY(Replicated)
	uint32 OrderSerial = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	TObjectPtr<AActor> AttackTarget;
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
	bool IsHoldTargetPermitted(const AArmyUnit& Target, const AMapRegion& Region, float MaximumWeaponRange) const;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	double GetHoldResponseStarted() const { return HoldClock.Started; }
	double GetHoldQuietSince() const { return HoldClock.QuietSince; }
#endif

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
	void UpdateHoldMovement(AArmyUnit& Unit, const AMapRegion& Region, UNavigationSystemV1* Navigation,
		const FVector& Goal, float Now);
	void UpdateHoldResponse(AArmyUnit& Unit, const AMapRegion& Region, UNavigationSystemV1* Navigation, float Now);
	bool CommitOrder(const FForceOrder& InOrder, bool bQueue, float SelectionSpeed);
	bool ApplyWaypoint(int32 RegionIndex, EArmyOrder Phase, AActor* Structure = nullptr);
	bool HasArrivedAtRegion(const ACommandGameState& State, int32 RegionIndex) const;
	void CompleteOrder(int32 EndRegion);
	void UpdateMarchSpeed();
	int32 LastHeldRegionIndex = INDEX_NONE;
	int32 WithdrawalRegionIndex = INDEX_NONE;
	bool bWithdrawing = false;
	bool bStructureAttack = false;
	bool bIdleRally = false;
	float NextWaypointAttempt = 0.f;
	float NextHoldingMaintenance = 0.f;
	int32 AppliedWaypoint = INDEX_NONE;
	EArmyOrder AppliedPhase = EArmyOrder::Hold;
	TWeakObjectPtr<AActor> AppliedStructure;
	bool IssueTravel(EArmyOrder NewOrder, const FVector& InDestination, bool bApply = true);
	void UpdateCombat();
	void UpdateReinforcements();
	FVector ReinforcementTarget(const AArmyUnit& Unit) const;
	float CombatAccumulator = 0.f;
	// Stable composition slots retain retry clocks through unit-array compaction.
	TArray<float, TInlineAllocator<6>> NextPursuitAttempts;
	bool bProducedGroup = false;
	int32 ForceCapacity = 0;
	void StopAllUnits();
	void LogOrder() const;
	FVector FormationOffset(int32 Index) const;
};
