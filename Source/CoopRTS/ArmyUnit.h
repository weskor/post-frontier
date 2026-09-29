#pragma once

#include "Engine/DataAsset.h"
#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "ArmyUnit.generated.h"

class AArmyGroup;
enum class EArmyDoctrine : uint8;
class UStaticMeshComponent;
class UStaticMesh;

UENUM(BlueprintType)
enum class EUnitRole : uint8
{
	Frontline,
	Ranged,
	Siege
};

UCLASS(BlueprintType)
class COOPRTS_API UArmyUnitDefinition : public UDataAsset
{
	GENERATED_BODY()
public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	EUnitRole Role = EUnitRole::Frontline;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 MaxHealth = 140;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	int32 AttackDamage = 14;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Range = 175.f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat")
	float Interval = .7f;
};

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

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<UArmyUnitDefinition> Definition;


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

	// Stable fixed-composition slot; casualties do not renumber survivors.
	UPROPERTY(Replicated)
	int32 CompositionSlot = -1;

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Army")
	TObjectPtr<UStaticMeshComponent> Body;

	// Themed meshes indexed by EUnitRole; an empty entry (asset missing) falls back to the cube.
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> HumanMeshes;
	UPROPERTY()
	TArray<TObjectPtr<UStaticMesh>> MachineMeshes;

	UFUNCTION()
	void OnRep_Appearance();
	UFUNCTION()
	void OnRep_Attack();

};
