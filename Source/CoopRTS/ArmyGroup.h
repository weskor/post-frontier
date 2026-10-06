#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ConstructionTypes.h"
#include "Rules/ArmyGroupPolicy.h"
#include "Rules/HoldPolicy.h"
#include "Rules/MovementProgressPolicy.h"
#include "Rules/SupplyDeliveryPolicy.h"
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
struct FArrivalLayout;

// A unit's movement progress toward its current goal (Rules/MovementProgressPolicy.h), keyed by composition slot.
struct FUnitProgressSlot
{
	TWeakObjectPtr<const AArmyUnit> Unit;
	MovementProgressPolicy::FUnitProgress Progress;
};

// The slots of a force fitted inside the region under its Destination, by composition slot.
struct FFittedSlots
{
	TArray<FVector, TInlineAllocator<6>> Goals;
	FVector Centre = FVector::ZeroVector;
};

// The last fitted set of a force. FitForce depends only on the region polygon, the centre and the slot layout, so
// the set is valid until one of them changes: a new region, a moved centre (a new post, a new order) or a new
// layout (capacity, produced or opposing). Membership does not enter: every slot is fitted, occupied or not.
struct FFittedCache
{
	bool bValid = false;
	const AMapRegion* Region = nullptr;
	FVector Centre = FVector::ZeroVector;
	ArmyGroupPolicy::FFormation Shape;
	FFittedSlots Slots;
};

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
	// Paid recruits join through QueueRecruit or SpawnReinforcement, never SpawnMember.
	AArmyUnit* SpawnMember(int32 UnitIndex, const FVector& SpawnLocation, int32 CompositionSlot);
	// The next unused army index: production and free forces take the next one each.
	static int32 NextArmyIndex(const UWorld& World);
	// A free force for the enemy commander (its waves) or a human commander (the emergency force): a
	// producerless group holding the listed catalogue units (at most six), placed on free navigable
	// ground around Anchor. It touches no wallet and no extraction. Null when nothing could be placed.
	static AArmyGroup* SpawnFreeForce(UWorld& World, ACommandPlayerState& Owner, const FVector& Anchor,
		TConstArrayView<int32> UnitIndices, int32 InForceNumber, float InSpeedFactor);
	// Spawns a recruit of the producer's unit on the producer's exit and joins it at once. Debits nothing.
	bool SpawnReinforcement(int32 UnitIndex, const FVector& SpawnLocation);
	// Supply-chain replacements (Rules/SupplyDeliveryPolicy.h), authority only. A force with living members gets
	// each recruit after SupplyDelivery::Delay; a cut-off force's recruit waits at the producer.
	// Debits one unit price from the producer's owner and queues the recruit; false (no debit) when it cannot.
	bool QueueRecruit(int32 UnitIndex);
	// An empty force has nobody to deliver to: its paid recruits wait for the producer's exit. True, at most
	// every 0.25 s, while one is waiting; the producer then offers its free exits to SpawnRecruitForExit.
	bool ClaimExitAttempt();
	// Spawns the oldest paid recruit at the producer's exit. Debits nothing.
	bool SpawnRecruitForExit(const FVector& Exit);
	// Recruits paid for and not yet joined: in transit plus waiting at the producer. Replicated.
	int32 GetPendingRecruitCount() const { return RecruitsInTransit + RecruitsWaiting; }
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Supply")
	int32 RecruitsInTransit = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Supply")
	int32 RecruitsWaiting = 0;
	// A producer-backed force with living members that the supply chain does not reach.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Supply")
	bool bSupplyCutOff = false;
	// A free force (SpawnFreeForce): never had a producer. For a human commander that is the emergency force of a
	// HQ gone offline, which its force card names.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	bool bFreeForce = false;
	void SettleMatch();
	FVector GetCenter() const;
	EArmyDoctrine GetDoctrine() const;

	void TickOrders();
	int32 GetCapacity() const;
	int32 GetAliveCount() const;
	int32 GetJoinedCount() const;
	float GetBaseMarchSpeed() const;
	float GetMarchSpeed() const;
	// Movement progress. A settled unit stopped making progress toward its goal for MovementProgressPolicy::SettleIdleSeconds
	// and no longer holds back its force's arrival or waypoint; it stays settled until its goal changes.
	bool IsUnitSettled(const AArmyUnit& Unit) const;
	// Radius around its goal inside which the unit counts as arrived: 0 while it moves freely, growing with idle time.
	float GetCloseEnoughRadius(const AArmyUnit& Unit) const;
	// Cohesion while marching: the largest planar distance of a member from the force's mean, and the largest
	// distance of a formation slot from its centre (the spread a formation is expected to have).
	float GetMarchSpread() const;
	float GetFormationRadius() const;
	// Units settled so far, cumulative; authority only.
	int32 GetSettledUnitCount() const { return UnitsSettled; }
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
	// bLane: a region waypoint is ordered on the force's lane around the anchor (LanePolicy), else on the anchor.
	bool ApplyWaypoint(int32 RegionIndex, EArmyOrder Phase, AActor* Structure = nullptr, bool bLane = false);
	// Takes the lowest lane no same-team force already heading for RegionIndex holds.
	void AssignLane(int32 RegionIndex, const FVector& Anchor);
	bool IssueOnLane(EArmyOrder Phase, const AMapRegion& Region, const FVector& Anchor);
	int32 LaneIndex = INDEX_NONE;
	FVector2D LaneHeading = FVector2D::ZeroVector;
	bool ShouldKeepWaypoint(const ACommandGameState& State, int32 RegionIndex, AActor* Structure) const;
	bool IssueTravelNearAnchor(EArmyOrder Phase, const FVector& Anchor, const AMapRegion& Region);
	void UpdateProgress();
	void ResetProgress();
	const FUnitProgressSlot* FindProgress(const AArmyUnit& Unit) const;
	FUnitProgressSlot& ProgressFor(const AArmyUnit& Unit);
	// Where the unit is going: its pursuit goal while it chases a target, else its formation slot.
	FVector UnitGoal(const AArmyUnit& Unit, const FFittedSlots& Fitted) const;
	// Settled, or idle inside its grown radius: such a unit does not hold back arrival or waypoint advance. Never true while pursuing.
	bool IsUnitExempt(const AArmyUnit& Unit) const;
	void RepathUnit(AArmyUnit& Unit, const FVector& Goal);
	TArray<FUnitProgressSlot, TInlineAllocator<6>> UnitProgress;
	int32 UnitsSettled = 0;
	FVector GetMarchCenter() const;
	// The region the executor treats the force as standing in: that of GetMarchCenter.
	int32 MarchSourceRegion(const ACommandGameState& State) const;
	bool HasArrivedAtRegion(const ACommandGameState& State, int32 RegionIndex) const;
	// A structure order's stand-off point is reached: the force's mean is within 170 cm of the fitted mean.
	bool HasReachedStandOff() const;
	// The slots of the force fitted inside the region that holds Destination (ArmyGroupPolicy::FitForce).
	void FitSlots(const ACommandGameState* State, FFittedSlots& Out) const;
	// The same around any centre inside Region, served from the cache while region, centre and layout are unchanged.
	void FitSlotsAt(const AMapRegion* Region, const FVector& Centre, FFittedSlots& Out) const;
	mutable FFittedCache FitCache;
	bool GatherArrival(const ACommandGameState& State, bool bSkipExempt, FArrivalLayout& Out) const;
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
	// When the applied waypoint may be ordered again (MovementProgressPolicy::EOrderKind).
	MovementProgressPolicy::FOrderClocks OrderClocks;
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
	void UpdateSupply();
	int32 SupplyHops(const ACommandGameState& State, bool bForceEmpty);
	void SyncSupplyCounts(bool bCutOff);
	void CancelRecruits();
	// Branch refit of the member in line: its composition slot and the delivery delay it is waiting out.
	void UpdateRefit(SupplyDelivery::ERoute Route, int32 Hops, double Now);
	void ApplyRefit(AArmyUnit& Unit, int32 BranchIndex, const UArmyUnitDefinition& Branch);
	int32 RefitSlot = INDEX_NONE;
	SupplyDelivery::FRecruit RefitTimer;
	bool DeliverRecruit();
	void JoinFormation(AArmyUnit& Unit, AAIController& AI, UNavigationSystemV1& Navigation);
	bool FormationSlotGoal(int32 Slot, FVector& Goal) const;
	bool CanAcceptRecruit(const ACommandGameState* State, int32 UnitIndex, int32 Capacity) const;
	AArmyUnit* SpawnJoined(const UArmyUnitDefinition& Definition, int32 UnitIndex, int32 Slot, const FVector& Ground);
	// Recruits paid for and not yet joined; authority only. The replicated counts mirror it.
	TArray<SupplyDelivery::FRecruit> PendingRecruits;
	// Last region the force stood in, so a stretch of ground outside every region neither cuts nor feeds it.
	int32 LastSupplyRegion = INDEX_NONE;
	double NextExitAttempt = 0.;
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
