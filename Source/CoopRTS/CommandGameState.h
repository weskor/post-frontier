#pragma once

#include "CoreMinimal.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
#include "GameFramework/GameStateBase.h"
#include "CommandGameState.generated.h"

class AArenaBounds;
class ACapturePoint;
class ACommandBuilding;
class ACommandPlayerState;
class AHeadquarters;
class AMapRegion;
class ADepositSite;
class UMatchContent;

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
	bool ValidateBuildingPlacement(int32 BuildingIndex, int32 Team, const FVector& Location, FString& OutReason) const;
	bool IsInBuildTerritory(int32 BuildingIndex, int32 Team, const FVector& Location) const;
	static constexpr int32 BaselineIncomePerSecond = 2;
	int32 GetBaselineIncomePerSecond() const;

	// Single definition catalogue for every peer; indices replicated by actors resolve here.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Content")
	TObjectPtr<UMatchContent> Content;
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
	FString EnemyPlan;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	FString EnemyPlanRationale;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	TObjectPtr<ACommandPlayerState> EnemyCommander;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Explicit listen-host verification fixture; never present in shipping games.
	bool bVerificationIncomePaused = false;
#endif

private:
	friend class FCommandService;
	ACommandBuilding* ApplyPlacement(int32 BuildingIndex, const FVector& Location,
		ACommandPlayerState* Commander, int32 Team, FString& OutReason);
	float IncomeElapsed = 0.f;
	// Tenths preserve fractional JEV credits without floating-point drift.
	int32 EnemyIncomeRemainderTenths = 0;
	float AudioLiveStartServerTime = 0.f;
	EMatchResult LastAudioMatchResult = EMatchResult::Ongoing;
	bool bMatchAudioInitialized = false;
	bool bOutcomeAudioPlayed = false;
	UFUNCTION()
	void OnRep_MatchResult();
};
