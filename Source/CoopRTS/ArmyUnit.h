#pragma once

#include "CoreMinimal.h"
#include "Content/UnitDefinition.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Rules/DamagePolicy.h"
#include "Rules/ArmyGroupPolicy.h"
#include "Rules/RegionTraitPolicy.h"
#include "Rules/ShieldPolicy.h"
#include "ArmyUnit.generated.h"

class AArmyGroup;
enum class EArmyDoctrine : uint8;
class UStaticMeshComponent;
class AMapRegion;
class ACommandGameState;
class UStaticMesh;

// Walking speed with the standing region's bonus (Open) on top of whatever speed the force order set.
UCLASS()
class COOPRTS_API UArmyUnitMovement : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	virtual float GetMaxSpeed() const override { return Super::GetMaxSpeed() * TraitSpeedMultiplier; }

	// Server-set from the force's region traits; 1 outside Open ground.
	float TraitSpeedMultiplier = 1.f;
};
UCLASS()
class COOPRTS_API AArmyUnit : public ACharacter
{
	GENERATED_BODY()

public:
	explicit AArmyUnit(const FObjectInitializer& ObjectInitializer);
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	void Initialize(AArmyGroup* InGroup, int32 InTeamIndex, int32 InCommanderIndex,
		int32 InArmyIndex, int32 InCompositionSlot, int32 InUnitIndex,
		UArmyUnitDefinition* InDefinition);
	AArmyGroup* GetGroup() const { return Group.Get(); }
	int32 GetTeamIndex() const { return TeamIndex; }
	int32 GetArmyIndex() const { return ArmyIndex; }
	int32 GetCommanderIndex() const { return CommanderIndex; }
	int32 GetCompositionSlot() const { return CompositionSlot; }
	int32 GetUnitIndex() const { return UnitIndex; }
	UArmyUnitDefinition* GetDefinition() const { return Definition.Get(); }
	EUnitRole GetUnitRole() const { return UnitRole; }
	// The unit's row in a formation: 0 melee front, 1 ranged middle, 2 artillery back (ArmyGroupPolicy::AssignSlots).
	int32 FormationClassRank() const { return UnitRole == EUnitRole::Frontline ? 0 : UnitRole == EUnitRole::Siege ? 2
																												  : 1; }
	EArmorClass GetArmorClass() const { return Definition->ArmorClass; }
	EDamageType GetDamageType() const { return Definition->DamageType; }
	int32 GetHealth() const { return Health; }
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	void FireAt(AActor* Victim);
	float WeaponRange() const;
	int32 MaxHealth() const;
	float AttackInterval() const;
	EArmyDoctrine GetDoctrine() const;
	bool IsAlive() const { return Health > 0; }
	int32 GetShield() const { return Shield; }
	int32 MaxShield() const;
	double GetLastPulseServerTime() const { return LastPulseServerTime; }
	// Wipes the shield without HP damage and restarts regen (the Scrambler pulse).
	void StripShield();
	// Attacker-independent damage (Hazard): shield first, then HP, no class or incoming multipliers.
	void ReceiveEnvironmentalDamage(int32 Damage);
	// Re-reads the region under the unit into the cache; Tick does this on a short clock.
	void RefreshRegion();
	ERegionTrait GetRegionTrait() const;
	// Visual identity only; combat/capture allegiance uses TeamIndex.
	static FLinearColor GetCommanderColor(int32 InCommanderIndex);

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat")
	TObjectPtr<AActor> Target;

	UPROPERTY(ReplicatedUsing = OnRep_Attack)
	uint32 AttackCount = 0;

	float NextAttackTime = 0.f;
	bool bPursuing = false;
	FVector PursuitGoal = FVector::ZeroVector;
	// Server-only: what the force's last march-leg plan decided (heading, shape, goal, time of the last turn). Every
	// member of a force carries the same copy, so the plan's hysteresis needs no field on the force.
	ArmyGroupPolicy::FLegMemory FormationMemory;
	// Server-only: the ground this member's last formation move was sent to (a march leg's planned slot or the point
	// it fell back to, or its idle post slot): where it is meant to stand. Zero until the first formation move.
	// Movement progress reads it as the member's goal instead of the composition slot.
	FVector FormationTarget = FVector::ZeroVector;
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
	// Current shield points; 0 for unshielded units. Regenerates 4 s after the last damage.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat", meta = (AllowPrivateAccess = "true"))
	int32 Shield = 0;
	// Server time of the last pulse cast, -1 before the first; clients draw the pulse ring from changes to it.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Pulse", meta = (AllowPrivateAccess = "true"))
	double LastPulseServerTime = -1.;
	UPROPERTY(VisibleAnywhere, Category = "Army")
	TObjectPtr<UStaticMeshComponent> Body;

	// Server-only shield state; Shield itself replicates.
	ShieldPolicy::FShieldClock ShieldClock;
	// Server-only region cache, refreshed on a short clock rather than per hit or frame.
	TWeakObjectPtr<const AMapRegion> CurrentRegion;
	float NextRegionRefreshTime = 0.f;
	float HazardInsideSeconds = 0.f;
	double NextPulseReadyAt = 0.;
	float NextPulseScanTime = 0.f;

	// Local baselines suppress initial replication and repeated appearance notifications.
	bool bAudioStateInitialized = false;
	bool bDeathAudioPlayed = false;
	int32 LastAudioHealth = 0;
	uint32 LastAudioAttackCount = 0;

	void ApplyDurabilityLoss(const DamagePolicy::FResult& Result, const AArmyUnit* Killer);
	void TickShield(float DeltaSeconds);
	void TickRegion(float DeltaSeconds);
	void UpdateTraitSpeed();
	void TickPulse();
	void CastPulse(const ACommandGameState& State);

	UFUNCTION()
	void OnRep_Appearance();
	UFUNCTION()
	void OnRep_Attack();
};
