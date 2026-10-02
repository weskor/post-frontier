#pragma once

#include "CoreMinimal.h"
#include "Rules/CombatPolicy.h"
#include "GameFramework/Actor.h"
#include "Headquarters.generated.h"

class AArmyUnit;
class UStaticMeshComponent;
class UStaticMesh;
class UBoxComponent;

UCLASS()
class COOPRTS_API AHeadquarters : public AActor
{
	GENERATED_BODY()
public:
	AHeadquarters();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	int32 MaxHealth() const { return 900; }
	bool IsAlive() const { return Health > 0; }
	EArmorClass GetArmorClass() const { return EArmorClass::Structure; }

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Match")
	int32 Health = 900;
	// Set on the placed level actor: 0 friendly, 5 enemy.
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Match")
	int32 TeamIndex = 0;
private:
	// Root: invisible 300x300x200 cm box that owns the ECC_Visibility hit test used by cursor targeting.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> HitBox;
	// Purely visual; never collides.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;
	// Themed HQ meshes; null (asset missing) falls back to the scaled cube.
	UPROPERTY()
	TObjectPtr<UStaticMesh> HumanMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> MachineMesh;
	// Local health snapshot: initial replication is not a damage event.
	bool bAudioStateInitialized = false;
	bool bDestroyedAudioPlayed = false;
	int32 LastAudioHealth = 0;
	float NextAlarmAudioTime = 0.f;
	UFUNCTION()
	void OnRep_Appearance();
};
