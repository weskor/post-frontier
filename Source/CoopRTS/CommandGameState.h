#pragma once

#include "CoreMinimal.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
#include "GameFramework/GameStateBase.h"
#include "Rules/PauseBudget.h"
#include "ForceOrders.h"
#include "GameState/GameStateEconomy.h"
#include "GameState/HoldDamageLedger.h"
#include "Content/UnitDefinition.h"
#include "CommandGameState.generated.h"

class AArenaBounds;
class ACapturePoint;
class ACommandBuilding;
class ACommandPlayerState;
class ACommandPlayerController;
class AHeadquarters;
class AMapRegion;
class ADepositSite;
class UMatchContent;
class UObjectiveAnnouncer;
class UMatchTelemetry;
class AArmyUnit;
class AArmyGroup;

USTRUCT(BlueprintType)
struct FJevPublishedPlan
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	int32 TicketNumber = 0;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AArmyGroup> Force;
	UPROPERTY(BlueprintReadOnly)
	int32 ForceNumber = 0;
	UPROPERTY(BlueprintReadOnly)
	EForceVerb Verb = EForceVerb::MoveHold;
	UPROPERTY(BlueprintReadOnly)
	int32 SourceRegionIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly)
	int32 TargetRegionIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AActor> TargetStructure;
	UPROPERTY(BlueprintReadOnly)
	int32 SizeBand = 2;
	UPROPERTY(BlueprintReadOnly)
	float EtaSeconds = 0.f;
	// Server world time at which EtaSeconds was computed: the countdown runs from here.
	UPROPERTY(BlueprintReadOnly)
	float EtaIssuedAt = 0.f;
	UPROPERTY(BlueprintReadOnly)
	float CommittedUntil = 0.f;
	UPROPERTY(BlueprintReadOnly)
	float RemainingCommitment = 0.f;
	UPROPERTY(BlueprintReadOnly)
	bool bEscalated = false;
	UPROPERTY(BlueprintReadOnly)
	FString Memo;
};
#if !UE_BUILD_SHIPPING
struct FJevPlanHistoryEntry
{
	FJevPublishedPlan Plan;
	float TimeSeconds = 0.f;
	bool bEscalation = false;
	int32 SourceController = INDEX_NONE;
	bool bOrderChanged = false;
	int32 ForceNumber = 0;
	FString TargetStructureName;
};
#endif

// One queued first order of a commander's kit force: applied when the force exists at 0:00.
USTRUCT(BlueprintType)
struct FPlanningOrder
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	EForceVerb Verb = EForceVerb::MoveHold;
	UPROPERTY(BlueprintReadOnly)
	int32 RegionIndex = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<AActor> Structure;
};

// One commander's pre-built kit during planning: a finished Barracks and Drill Rig, the Barracks unit type and
// the first orders, all editable until Ready. JEV's matching kits carry the enemy wallet and are always Ready.
USTRUCT(BlueprintType)
struct FPlanningKit
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ACommandPlayerState> Commander;
	UPROPERTY(BlueprintReadOnly)
	bool bReady = false;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ACommandBuilding> Barracks;
	UPROPERTY(BlueprintReadOnly)
	TObjectPtr<ACommandBuilding> Rig;
	UPROPERTY(BlueprintReadOnly)
	EUnitRole UnitRole = EUnitRole::Frontline;
	UPROPERTY(BlueprintReadOnly)
	TArray<FPlanningOrder> Orders;
};

// The phase before 0:00 (battle.md "Opening"). SecondsRemaining counts real time down while the world is frozen.
USTRUCT(BlueprintType)
struct FPlanningState
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	bool bActive = false;
	UPROPERTY(BlueprintReadOnly)
	float SecondsRemaining = 0.f;
	UPROPERTY(BlueprintReadOnly)
	TArray<FPlanningKit> Kits;
	UPROPERTY(BlueprintReadOnly)
	TArray<FPlanningKit> JevKits;
};

enum class EPlanningEnd : uint8
{
	None,
	AllReady,
	Expired,
	// A simulation completed it at once with default kits.
	Harness,
	// An automation fixture skipped it: no kit and the legacy wallet.
	Fixture
};

// The regions a team's main reaches through its own regions, with the server time that set changed.
USTRUCT()
struct FTeamConnection
{
	GENERATED_BODY()
	// Blueprints have no 64-bit integer; C++ reads the mask.
	UPROPERTY()
	uint64 Mask = 0;
	UPROPERTY()
	float ChangedAt = 0.f;
};

// One accepted gift. Commanders are identified by slot because the log outlives a departed player state.
USTRUCT(BlueprintType)
struct FGiftLogEntry
{
	GENERATED_BODY()
	UPROPERTY(BlueprintReadOnly)
	int32 SenderSlot = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly)
	int32 RecipientSlot = INDEX_NONE;
	UPROPERTY(BlueprintReadOnly)
	EEconomyResource Resource = EEconomyResource::Power;
	UPROPERTY(BlueprintReadOnly)
	int32 Amount = 0;
	UPROPERTY(BlueprintReadOnly)
	float ServerTime = 0.f;
};

UENUM(BlueprintType)
enum class EMatchResult : uint8
{
	Ongoing,
	Victory,
	Defeat
};

UCLASS()
class COOPRTS_API ACommandGameState : public AGameStateBase
{
	GENERATED_BODY()
public:
	ACommandGameState();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void AddPlayerState(APlayerState* PlayerState) override;
	void SetMatchResult(EMatchResult Result);
	// Planning runs from the start of a battle until every human is Ready or 60 real seconds pass; nothing
	// advances meanwhile. Its commands are FPlanningCommands.
	bool IsPlanning() const { return Planning.bActive; }
	// The server world time at which the battle clock reads 0:00: now while planning, then the end of planning.
	float GetBattleClockStartServerTime() const;
	// Opens a fresh planning phase: pauses the world, resets wallets to the opening and gives every human a
	// kit slot and JEV its matching start. Server only.
	void BeginPlanning();
	// Opens the phase when this game state next ticks (the mode calls it at BeginPlay). Server only.
	void OpenPlanningOnNextTick() { bPlanningPending = true; }
	// True from the mode's request until the phase ends.
	bool IsPlanningOpenOrPending() const { return bPlanningPending || Planning.bActive; }
	// After 0:00 a joining commander gets a finished kit at default spots, with production started. Server only.
	void GrantLateKit(ACommandPlayerState* Commander);
	int32 GetPlanningEndCount() const { return PlanningEndCount; }
	EPlanningEnd GetPlanningEnd() const { return PlanningEndReason; }
	// Real seconds the last planning phase lasted.
	double GetPlanningSeconds() const { return PlanningSeconds; }
	const FPlanningKit* FindKit(const ACommandPlayerState* Commander) const;
	FPlanningKit* FindKit(const ACommandPlayerState* Commander);
	bool IsActivePaused() const { return bActivePaused; }
	bool IsCoopPauseSpent() const { return bCoopPauseSpent; }
	float GetPauseSecondsRemaining() const { return PauseSecondsRemaining; }
	// Menu pause is separate from active pause; closing Esc must not resume P.
	void RefreshSoloMenuPause(ACommandPlayerController* Controller, bool bMenuPaused);
	float GetAudioLiveStartServerTime() const
	{
		return bMatchAudioInitialized ? AudioLiveStartServerTime : GetServerWorldTimeSeconds();
	}
	// Whole Power per second for the commander, rounded down; GetPowerRate and GetDataRate are exact.
	int32 GetIncomePerSecond(const ACommandPlayerState* Commander) const;
	int32 GetEnemyIncomePerSecond() const;
	double GetEnemyBaselineIncomePerSecond() const;
	// Exact per-second share of the team pool a human commander receives, or JEV's own income.
	double GetPowerRate(const ACommandPlayerState* Commander) const;
	double GetDataRate(const ACommandPlayerState* Commander) const;
	// Mask and change time of a team's connected regions (team 0 or 5; other teams are never connected).
	uint64 GetConnectedMask(int32 Team) const;
	float GetConnectionChangedAt(int32 Team) const;
	bool IsRegionConnected(int32 Team, int32 RegionIndex) const;
	const AMapRegion* FindRegionAt(const FVector& Location) const;
	int32 GetRegionController(int32 RegionIndex) const;
	bool IsRegionContested(int32 RegionIndex, int32 ForTeam) const;
	FVector GetRegionAnchor(int32 RegionIndex) const;
	FVector ResolveBuildingLocation(int32 BuildingIndex, const FVector& RequestedLocation, int32 Team = 0) const;
	void NotifyRegionDamage(AActor* Victim, int32 VictimTeam, AArmyUnit* Attacker);
	bool IsDamagingRegion(const AArmyUnit& Attacker, int32 RegionIndex, int32 DefendingTeam) const;
	bool ValidateBuildingPlacement(int32 BuildingIndex, int32 Team, const FVector& Location, FString& OutReason) const;
	bool IsInBuildTerritory(int32 BuildingIndex, int32 Team, const FVector& Location) const;
	int32 GetHumanBaselineIncomePerSecond() const;
	int32 GetJevBaselineIncomePerSecond() const;

	// Single definition catalogue for every peer; indices replicated by actors resolve here.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Content")
	TObjectPtr<UMatchContent> Content;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Objectives")
	TObjectPtr<UObjectiveAnnouncer> ObjectiveAnnouncer;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Match")
	TObjectPtr<UMatchTelemetry> MatchTelemetry;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<TObjectPtr<AMapRegion>> Regions;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Economy")
	TArray<TObjectPtr<ADepositSite>> Deposits;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<TObjectPtr<ACapturePoint>> CaptureSites;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<TObjectPtr<ACommandBuilding>> Buildings;
	UPROPERTY(ReplicatedUsing = OnRep_MatchResult, BlueprintReadOnly, Category = "Match")
	EMatchResult MatchResult = EMatchResult::Ongoing;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	TObjectPtr<AHeadquarters> FriendlyHeadquarters;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	TObjectPtr<AHeadquarters> EnemyHeadquarters;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	TObjectPtr<AArenaBounds> Arena;
	UPROPERTY(Replicated)
	FTeamConnection HumanConnection;
	UPROPERTY(Replicated)
	FTeamConnection EnemyConnection;
	// The latest accepted gifts, oldest first, at most EconomyPolicy::GiftLogLimit.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Economy")
	TArray<FGiftLogEntry> GiftLog;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	TArray<FJevPublishedPlan> EnemyPlans;
#if !UE_BUILD_SHIPPING
	TArray<FJevPlanHistoryEntry> EnemyPlanHistory;
#endif
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	TObjectPtr<ACommandPlayerState> EnemyCommander;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Planning")
	FPlanningState Planning;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Explicit listen-host verification fixture; never present in shipping games.
	bool bVerificationIncomePaused = false;
#endif

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Ends an active planning phase at once as a harness would: the legacy start (no kit, 600 Power) for a
	// fixture, or all Ready with default kits when bWithKits. A no-op once planning is over.
	void CompletePlanningForHarness(bool bWithKits);
	// A planning test keeps the harness from completing planning, in this world and after a restart.
	static bool bPlanningHeldByTest;
#endif
private:
	friend class FCommandService;
	friend struct FPlanningCommands;
	bool ApplyPause(ACommandPlayerController* Controller, bool bPause);
	void PublishPauseBudget();
	FPauseBudget PauseBudget;
	UPROPERTY(Replicated)
	bool bActivePaused = false;
	UPROPERTY(Replicated)
	bool bCoopPauseSpent = false;
	UPROPERTY(Replicated)
	float PauseSecondsRemaining = 0.f;
	// Planning (GameState/GameStatePlanning.cpp). ApplyKitPlacement is a free, finished placement.
	ACommandBuilding* ApplyKitPlacement(int32 BuildingIndex, const FVector& Location, ACommandPlayerState* Commander,
		int32 Team, FString& OutReason);
	void TickPlanning();
	void ReconcilePlanningRoster();
	void EvaluatePlanningEnd();
	void EndPlanning(EPlanningEnd Reason);
	void FillKits();
	void FillKit(FPlanningKit& Kit);
	void StartKitForces();
	void PlaceJevKit(bool bForce);
	// Places or moves one kit piece; a refused move leaves the piece where it was.
	bool PlaceKitPiece(FPlanningKit& Kit, bool bRig, const FVector& Location, FString& OutReason);
	bool PlaceDefaultBarracks(FPlanningKit& Kit);
	bool PlaceDefaultRig(FPlanningKit& Kit);
	void DestroyKit(FPlanningKit& Kit);
	void SyncWorldPause(ACommandPlayerController* Controller);
	bool bPlanningPending = false;
	double JevKitRetryAt = 0.;
	double PlanningDeadline = 0.;
	double PlanningStartedReal = 0.;
	double PlanningSeconds = 0.;
	int32 PlanningEndCount = 0;
	EPlanningEnd PlanningEndReason = EPlanningEnd::None;
	// Stamped when planning ends; negative until then, when the clock starts with the world.
	UPROPERTY(Replicated)
	float BattleClockStartServerTime = -1.f;
	bool bSoloMenuPaused = false;
	ACommandBuilding* ApplyPlacement(int32 BuildingIndex, const FVector& Location,
		ACommandPlayerState* Commander, int32 Team, FString& OutReason);
	FGameStateEconomy Economy;
	void UpdateRegionAlarms();
	// Hold-alarm pass over regions, defined in GameState/GameStateHold.cpp.
	struct FHoldAlarmPass;
	FHoldDamageLedger HoldDamage;
	float HoldAlarmElapsed = 0.f;
	float AudioLiveStartServerTime = 0.f;
	EMatchResult LastAudioMatchResult = EMatchResult::Ongoing;
	bool bMatchAudioInitialized = false;
	bool bOutcomeAudioPlayed = false;
	UFUNCTION()
	void OnRep_MatchResult();
};
