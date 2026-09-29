#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArmyGroup.generated.h"

class AArmyUnit;
enum class EArmyDoctrine : uint8;
class UArmyUnitDefinition;
class ACommandPlayerState;

UENUM(BlueprintType)
enum class EArmyOrder : uint8
{
	Hold,
	Move,
	Attack,
	Retreat
};

UCLASS()
class COOPRTS_API AArmyGroup : public AActor
{
	GENERATED_BODY()

public:
	AArmyGroup();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	bool SpawnUnits();
	int32 GetReinforcementCost() const;
	bool GetReinforcementSource(FVector& OutLocation, bool& bBase) const;
	bool CanReinforceAtCurrentLocation() const;
	FString GetReinforcementStatus() const;
	bool TryReinforce();
	bool IssueMove(FVector InDestination);
	bool IssueAttack(FVector InDestination, AActor* InTarget);
	bool IssueHold();
	bool IssueRetreat();
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
	TObjectPtr<AActor> AttackTarget;

	static constexpr float PursuitRadius = 1050.f;
	bool bOpposingArmy = false;


protected:
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UPROPERTY()
	TObjectPtr<UArmyUnitDefinition> FrontlineDefinition;
	UPROPERTY()
	TObjectPtr<UArmyUnitDefinition> RangedDefinition;
	UPROPERTY()
	TObjectPtr<UArmyUnitDefinition> SiegeDefinition;

	bool IssueTravel(EArmyOrder NewOrder, const FVector& InDestination);
	void UpdateCombat();
	float CombatAccumulator = 0.f;
	void StopAllUnits();
	void LogOrder() const;
	FVector FormationOffset(int32 Index) const;
};
