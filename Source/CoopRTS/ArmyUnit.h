#pragma once

#include "CoreMinimal.h"
#include "Content/UnitDefinition.h"
#include "GameFramework/Character.h"
#include "ArmyUnit.generated.h"

class AArmyGroup;
enum class EArmyDoctrine : uint8;
class UStaticMeshComponent;
class UStaticMesh;

UCLASS()
class COOPRTS_API AArmyUnit : public ACharacter
{
	GENERATED_BODY()

public:
	AArmyUnit();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	void FireAt(AActor* Victim);
	float WeaponRange() const;
	int32 MaxHealth() const;
	float AttackInterval() const;
	EArmyDoctrine GetDoctrine() const;
	bool IsAlive() const { return Health > 0; }
	// Visual identity only; combat/capture allegiance uses TeamIndex.
	static FLinearColor GetCommanderColor(int32 InCommanderIndex);

	// Index into ACommandGameState::Content->Units; the identity replicated to peers.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat")
	int32 UnitIndex = -1;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UArmyUnitDefinition> Definition;

	// Derived from Definition->Role on spawn; combat and doctrine logic branch on it.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Combat")
	EUnitRole UnitRole = EUnitRole::Frontline;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Combat")
	int32 Health = 100;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> Target;

	UPROPERTY(ReplicatedUsing = OnRep_Attack)
	uint32 AttackCount = 0;

	float NextAttackTime = 0.f;
	bool bPursuing = false;
	FVector PursuitGoal = FVector::ZeroVector;
	// Server-only Field Repairs state; interruptions reset both clocks.
	float QuietSeconds = 0.f;
	float HealAccumulator = 0.f;
	void ResetRepairTimer();


	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	TObjectPtr<AArmyGroup> Group;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Army")
	int32 TeamIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Army")
	int32 ArmyIndex = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Army")
	int32 CommanderIndex = -1;

	// Stable unique slot within the producer's capacity; fixtures retain slots 0..5.
	UPROPERTY(Replicated)
	int32 CompositionSlot = -1;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army")
	bool bReinforcing = false;
	// Authority-only desired rendezvous and its accepted navigation projection.
	FVector ReinforcementGoal = FVector::ZeroVector;
	FVector ReinforcementRendezvous = FVector::ZeroVector;
	bool bHasReinforcementPath = false;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Army")
	TObjectPtr<UStaticMeshComponent> Body;

	UFUNCTION()
	void OnRep_Appearance();
	UFUNCTION()
	void OnRep_Attack();

};
