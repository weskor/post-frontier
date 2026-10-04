#pragma once

#include "CoreMinimal.h"
#include "Rules/CombatPolicy.h"
#include "GameFramework/Actor.h"
#include "FailoverNode.generated.h"

class AArmyUnit;
class AEnemyCommander;
class AHeadquarters;
class UBoxComponent;
class UStaticMesh;
class UStaticMeshComponent;

// A Failover Node: one of the two pre-built structures that guard an HQ (HqHoldPolicy). The HQ takes no
// damage while either stands. Never built, repaired, rebuilt or stunned; a valid Attack target for both sides.
UCLASS()
class COOPRTS_API AFailoverNode : public AActor
{
	GENERATED_BODY()
public:
	AFailoverNode();
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// The same damage pipeline as every structure: incoming multipliers (Fortify, plating), then HP.
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	int32 MaxHealth() const;
	bool IsAlive() const { return Health > 0; }
	EArmorClass GetArmorClass() const { return EArmorClass::Structure; }
	// The HQ this node guards: the headquarters of its team, found once; null before the level has it.
	AHeadquarters* GetHome() const;
	// Whether the 90% damage reduction of the opening still applies, read through the JEV match clock.
	bool IsPlated() const;
	// Seconds of battle time on that clock; 0 while no commander exists.
	float GetBattleSeconds() const;
	// The hit box is a cube of this half size; the themed mesh stands on the floor of it.
	static constexpr float HitBoxHalfSize = 150.f;

	UPROPERTY(ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Match")
	int32 Health = 1000;
	// Set on the placed level actor: 0 Hardline, 5 Lattice.
	UPROPERTY(EditAnywhere, ReplicatedUsing = OnRep_Appearance, BlueprintReadOnly, Category = "Match")
	int32 TeamIndex = 0;

private:
	// Root: invisible box that owns the ECC_Visibility hit test used by cursor targeting.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UBoxComponent> HitBox;
	// Purely visual; never collides. The themed mesh's pivot is the centre of its base.
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Body;
	UPROPERTY()
	TObjectPtr<UStaticMesh> HumanMesh;
	UPROPERTY()
	TObjectPtr<UStaticMesh> MachineMesh;
	mutable TWeakObjectPtr<AEnemyCommander> Clock;
	mutable TWeakObjectPtr<AHeadquarters> Home;
	bool bAudioStateInitialized = false;
	int32 LastAudioHealth = 0;
	UFUNCTION()
	void OnRep_Appearance();
	void AnnounceLoss(AArmyUnit* Attacker) const;
};
