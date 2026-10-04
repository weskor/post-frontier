#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ConstructionTypes.h"
#include "Rules/ArmyGroupPolicy.h"
#include "Rules/HoldPolicy.h"
#include "ForceOrders.h"
#include "ArmyGroup.generated.h"

class AAIController;
class AArmyUnit;
class ACommandBuilding;
class ACommandGameState;
enum class EArmyDoctrine : uint8;
class ACommandPlayerState;
class AMapRegion;
class UArmyUnitDefinition;
class UNavigationSystemV1;
struct FArmyCombatScan;
struct FForceTickContext;

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
	friend struct FArmyMovementTestAccess;
#endif
	// Authority-owned encounters may create joined members without a producer.
	// Paid reinforcement continues to use SpawnReinforcement exclusively.
	AArmyUnit* SpawnMember(int32 UnitIndex, const FVector& SpawnLocation, int32 CompositionSlot);
	// A free force for the enemy commander: a producerless group holding the listed catalogue
	// units (at most six), placed on free navigable ground around Anchor. It touches no wallet
	// and no extraction. Null when nothing could be placed.
	static AArmyGroup* SpawnFreeForce(UWorld& World, ACommandPlayerState& Owner, const FVector& Anchor,
		TConstArrayView<int32> UnitIndices, int32 InForceNumber, float InSpeedFactor);
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
	// Executor-selected safe endpoint; Retreat commands intentionally have no regional target.
	int32 GetRetreatRegion() const { return WithdrawalRegionIndex; }
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
	// Multiplies the base march speed of every member: 1 for ordinary forces, set once when a
	// free wave force spawns (JEV v2.1). Composes with region traits and the selection cap.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	float SpeedFactor = 1.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	int32 WaypointRegionIndex = INDEX_NONE;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Orders")
	int32 ResumeCount = 0;

	const TArray<FForceRoute>& GetIntentRoutes() const { return IntentRoutes; }

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
	void RetargetIdleRally(int32 RegionIndex);
	bool ApplyWaypoint(int32 RegionIndex, EArmyOrder Phase, AActor* Structure = nullptr);
	bool ShouldKeepWaypoint(const ACommandGameState& State, int32 RegionIndex, AActor* Structure) const;
	bool IssueTravelNearAnchor(EArmyOrder Phase, const FVector& Anchor, const AMapRegion& Region);
	bool HasArrivedAtRegion(const ACommandGameState& State, int32 RegionIndex) const;
	void CompleteOrder(int32 EndRegion);
	// Completes the active order, then ticks the next one.
	void AdvanceOrder(int32 EndRegion);
	void EnsureActiveOrder(int32 Source);
	void CancelOrphanRally(int32 Source);
	void ReadTickContext(FForceTickContext& Ctx);
	bool TickWithdrawal(const FForceTickContext& Ctx);
	bool TickTarget(const FForceTickContext& Ctx);
	bool TickHold(const FForceTickContext& Ctx, const AMapRegion& Target, bool bCleared);
	void MaintainHoldWaypoint(const FForceTickContext& Ctx, const AMapRegion& Target);
	void TickMarch(const FForceTickContext& Ctx);
	void FinishTick(const FForceTickContext& Ctx);
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
	UPROPERTY(Replicated)
	TArray<FForceRoute> IntentRoutes;
	void UpdateIntentRoutes(const uint64* Graph, int32 Count, int32 Source, uint64 Controlled, uint64 Hostiles, bool bTargetCompleted);
	bool IssueTravel(EArmyOrder NewOrder, const FVector& InDestination, bool bApply = true);
	void UpdateCombat();
	void UpdateUnitCombat(AArmyUnit& Unit, const FArmyCombatScan& Scan);
	bool IsEngagementPermitted(const AArmyUnit& Unit, AActor* Enemy) const;
	AActor* ChooseTarget(AArmyUnit& Unit, const FArmyCombatScan& Scan) const;
	void UpdateAttackPursuit(AArmyUnit& Unit, AAIController& AI, AActor& Chosen, bool bTargetChanged);
	void UpdateReinforcements();
	void UpdateReinforcement(AArmyUnit& Unit, UNavigationSystemV1& Navigation);
	bool JoinFormation(AArmyUnit& Unit, AAIController& AI, UNavigationSystemV1& Navigation);
	bool ReinforcementTarget(const AArmyUnit& Unit, FVector& Goal) const;
	void AbandonReinforcementMove(AArmyUnit& Unit, AAIController& AI);
	bool RetargetReinforcement(AArmyUnit& Unit, AAIController& AI, UNavigationSystemV1& Navigation, const FVector& Goal);
	bool CanSpawnReinforcement(const ACommandGameState* State, int32 UnitIndex, int32 Capacity, const FVector& SpawnLocation);
	bool LaunchReinforcement(UNavigationSystemV1& Navigation, const UArmyUnitDefinition& Definition,
		int32 UnitIndex, int32 Slot, int32 Capacity, const FVector& Exit);
	bool HasPermittedOwner(const ACommandGameState& State) const;
	bool ClipHoldingDestination(FVector& Goal) const;
	float CombatAccumulator = 0.f;
	// Stable composition slots retain retry clocks through unit-array compaction.
	TArray<float, TInlineAllocator<6>> NextPursuitAttempts;
	bool bProducedGroup = false;
	int32 ForceCapacity = 0;
	void StopAllUnits();
	void LogOrder() const;
	FVector FormationOffset(int32 Index) const;
	ArmyGroupPolicy::FFormation FormationShape() const { return { bProducedGroup, ForceCapacity, bOpposingArmy }; }
	// Retry clock for a composition slot, grown on demand. Do not hold the reference across a call that may grow it.
	float& PursuitRetryAt(int32 Slot);
};
