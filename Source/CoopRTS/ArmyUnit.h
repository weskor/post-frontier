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
	void Initialize(AArmyGroup* InGroup, int32 InTeamIndex, int32 InCommanderIndex,
		int32 InArmyIndex, int32 InCompositionSlot, int32 InUnitIndex,
		UArmyUnitDefinition* InDefinition, bool bInReinforcing = false);
	AArmyGroup* GetGroup() const { return Group.Get(); }
	int32 GetTeamIndex() const { return TeamIndex; }
	int32 GetArmyIndex() const { return ArmyIndex; }
	int32 GetCommanderIndex() const { return CommanderIndex; }
	int32 GetCompositionSlot() const { return CompositionSlot; }
	int32 GetUnitIndex() const { return UnitIndex; }
	UArmyUnitDefinition* GetDefinition() const { return Definition.Get(); }
	EUnitRole GetUnitRole() const { return UnitRole; }
	int32 GetHealth() const { return Health; }
	bool IsReinforcing() const { return bReinforcing; }
	const FVector& GetReinforcementGoal() const { return ReinforcementGoal; }
	const FVector& GetReinforcementRendezvous() const { return ReinforcementRendezvous; }
	bool HasReinforcementPath() const { return bHasReinforcementPath; }
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	void FireAt(AActor* Victim);
	float WeaponRange() const;
	int32 MaxHealth() const;
	float AttackInterval() const;
	EArmyDoctrine GetDoctrine() const;
	bool IsAlive() const { return Health > 0; }
	// Visual identity only; combat/capture allegiance uses TeamIndex.
	static FLinearColor GetCommanderColor(int32 InCommanderIndex);


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



protected:
	virtual void BeginPlay() override;

private:
	friend class AArmyGroup;

	// Index into ACommandGameState::Content->Units; the identity replicated to peers.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	int32 UnitIndex = -1;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UArmyUnitDefinition> Definition;

	// Derived from Definition->Role on spawn; combat and doctrine logic branch on it.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	EUnitRole UnitRole = EUnitRole::Frontline;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	int32 Health = 100;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<AArmyGroup> Group;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Army", meta = (AllowPrivateAccess = "true"))
	int32 TeamIndex = 0;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Army", meta = (AllowPrivateAccess = "true"))
	int32 ArmyIndex = 0;
	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Army", meta = (AllowPrivateAccess = "true"))
	int32 CommanderIndex = -1;

	// Stable unique slot within the producer's capacity; fixtures retain slots 0..5.
	UPROPERTY(Replicated)
	int32 CompositionSlot = -1;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Army", meta = (AllowPrivateAccess = "true"))
	bool bReinforcing = false;
	// Authority-only desired rendezvous and its accepted navigation projection.
	FVector ReinforcementGoal = FVector::ZeroVector;
	FVector ReinforcementRendezvous = FVector::ZeroVector;
	bool bHasReinforcementPath = false;
	UPROPERTY(VisibleAnywhere, Category = "Army")
	TObjectPtr<UStaticMeshComponent> Body;

	UFUNCTION()
	void OnRep_Appearance();
	UFUNCTION()
	void OnRep_Attack();

};
