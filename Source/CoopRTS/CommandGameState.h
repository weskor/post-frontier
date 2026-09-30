#pragma once

#include "CoreMinimal.h"
#include "CommandPlayerState.h"
#include "ConstructionTypes.h"
#include "GameFramework/GameStateBase.h"
#include "CommandGameState.generated.h"

class ACapturePoint;
class ACommandBuilding;
class ACommandPlayerState;
class AHeadquarters;

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
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void RefreshTerritory();
	int32 GetIncomePerSecond() const { return BaselineIncomePerSecond + ResourceIncomePerSecond * ControlledResourceSites; }
	int32 GetEnemyIncomePerSecond() const;
	bool ValidateBuildingPlacement(EBuildingKind Kind, int32 Team, const FVector& Location, FString& OutReason) const;
	ACommandBuilding* TryPlaceBuilding(EBuildingKind Kind, const FVector& Location,
		ACommandPlayerState* Commander, int32 Team, FString& OutReason);
	static constexpr int32 BaselineIncomePerSecond = 10;
	static constexpr int32 ResourceIncomePerSecond = 6;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	int32 ControlledResourceSites = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<TObjectPtr<ACapturePoint>> CaptureSites;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<TObjectPtr<ACommandBuilding>> Buildings;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	EMatchResult MatchResult = EMatchResult::Ongoing;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	TObjectPtr<AHeadquarters> FriendlyHeadquarters;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Match")
	TObjectPtr<AHeadquarters> EnemyHeadquarters;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	FString EnemyPlan;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	FString EnemyPlanRationale;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	int32 EnemyResources = ACommandPlayerState::InitialResources;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Enemy")
	EArmyDoctrine EnemyDoctrine = EArmyDoctrine::None;
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Explicit listen-host verification fixture; never present in shipping games.
	bool bVerificationIncomePaused = false;
#endif

private:
	float IncomeElapsed = 0.f;
};
