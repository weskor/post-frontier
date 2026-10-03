#pragma once

#include "CoreMinimal.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
#include "GameFramework/GameStateBase.h"
#include "Rules/PauseBudget.h"
#include "ForceOrders.h"
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
	int32 ForceNumber = 0;
	FString TargetStructureName;
};
#endif

struct FHoldDamageSource
{
	TWeakObjectPtr<AArmyUnit> Attacker;
	TWeakObjectPtr<AActor> Victim;
	int32 Team = INDEX_NONE;
	int32 Region = INDEX_NONE;
	double Expires = 0.;
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
	bool IsActivePaused() const { return bActivePaused; }
	bool IsCoopPauseSpent() const { return bCoopPauseSpent; }
	float GetPauseSecondsRemaining() const { return PauseSecondsRemaining; }
	// Menu pause is separate from active pause; closing Esc must not resume P.
	void RefreshSoloMenuPause(ACommandPlayerController* Controller, bool bMenuPaused);
	float GetAudioLiveStartServerTime() const
	{
		return bMatchAudioInitialized ? AudioLiveStartServerTime : GetServerWorldTimeSeconds();
	}
	int32 GetIncomePerSecond(const ACommandPlayerState* Commander) const;
	int32 GetEnemyIncomePerSecond() const;
	double GetEnemyBaselineIncomePerSecond() const;
	const AMapRegion* FindRegionAt(const FVector& Location) const;
	int32 GetRegionController(int32 RegionIndex) const;
	bool IsRegionContested(int32 RegionIndex, int32 ForTeam) const;
	FVector GetRegionAnchor(int32 RegionIndex) const;
	FVector ResolveBuildingLocation(int32 BuildingIndex, const FVector& RequestedLocation, int32 Team = 0) const;
	void NotifyRegionDamage(AActor* Victim, int32 VictimTeam, AArmyUnit* Attacker);
	bool IsDamagingRegion(const AArmyUnit& Attacker, int32 RegionIndex, int32 DefendingTeam) const;
	bool ValidateBuildingPlacement(int32 BuildingIndex, int32 Team, const FVector& Location, FString& OutReason) const;
	bool IsInBuildTerritory(int32 BuildingIndex, int32 Team, const FVector& Location) const;
	int32 GetBaselineIncomePerSecond() const;

	// Single definition catalogue for every peer; indices replicated by actors resolve here.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Content")
	TObjectPtr<UMatchContent> Content;
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Objectives")
	TObjectPtr<UObjectiveAnnouncer> ObjectiveAnnouncer;
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
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	TArray<FJevPublishedPlan> EnemyPlans;
#if !UE_BUILD_SHIPPING
	TArray<FJevPlanHistoryEntry> EnemyPlanHistory;
#endif
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	TObjectPtr<ACommandPlayerState> EnemyCommander;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Explicit listen-host verification fixture; never present in shipping games.
	bool bVerificationIncomePaused = false;
#endif

private:
	friend class FCommandService;
	bool ApplyPause(ACommandPlayerController* Controller, bool bPause);
	void PublishPauseBudget();
	FPauseBudget PauseBudget;
	UPROPERTY(Replicated)
	bool bActivePaused = false;
	UPROPERTY(Replicated)
	bool bCoopPauseSpent = false;
	UPROPERTY(Replicated)
	float PauseSecondsRemaining = 0.f;
	bool bSoloMenuPaused = false;
	ACommandBuilding* ApplyPlacement(int32 BuildingIndex, const FVector& Location,
		ACommandPlayerState* Commander, int32 Team, FString& OutReason);
	float IncomeElapsed = 0.f;
	void UpdateRegionAlarms();
	TArray<FHoldDamageSource> HoldDamageSources;
	float HoldAlarmElapsed = 0.f;
	// Tenths preserve fractional JEV credits without floating-point drift.
	int32 EnemyIncomeRemainderTenths = 0;
	float AudioLiveStartServerTime = 0.f;
	EMatchResult LastAudioMatchResult = EMatchResult::Ongoing;
	bool bMatchAudioInitialized = false;
	bool bOutcomeAudioPlayed = false;
	UFUNCTION()
	void OnRep_MatchResult();
};
