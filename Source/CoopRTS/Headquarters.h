#pragma once

#include "CoreMinimal.h"
#include "Rules/CombatPolicy.h"
#include "Rules/HqHoldPolicy.h"
#include "GameFramework/Actor.h"
#include "Headquarters.generated.h"

class AArmyUnit;
class ACommandGameState;
class AFailoverNode;
class UStaticMeshComponent;
class UStaticMesh;
class UBoxComponent;

// An HQ and its lifecycle (HqHoldPolicy::EPhase): online, offline at 0 HP while the attackers hold its
// main, lost once the hold completes. Two Failover Nodes guard it: no damage while either stands.
UCLASS()
class COOPRTS_API AHeadquarters : public AActor
{
	GENERATED_BODY()
public:
	AHeadquarters();
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// Damage is refused while a node stands or the HQ is not online. At 0 HP the HQ goes offline.
	void ReceiveAttack(int32 Damage, AArmyUnit* Attacker);
	int32 MaxHealth() const { return 900; }
	// Not lost: the single source territory, connectivity, placement and the outcome read. An offline HQ is alive.
	bool IsAlive() const { return GetPhase() != HqHoldPolicy::EPhase::Lost; }
	bool IsOnline() const { return GetPhase() == HqHoldPolicy::EPhase::Online; }
	bool IsOffline() const { return GetPhase() == HqHoldPolicy::EPhase::Offline; }
	HqHoldPolicy::EPhase GetPhase() const { return static_cast<HqHoldPolicy::EPhase>(Phase); }
	HqHoldPolicy::FHold GetHold() const { return { HoldProgress, bHoldStarted }; }
	HqHoldPolicy::EHoldState GetHoldState() const { return static_cast<HqHoldPolicy::EHoldState>(HoldState); }
	EArmorClass GetArmorClass() const { return EArmorClass::Structure; }
	// The hit box is a square of this half size, turned with the actor.
	static constexpr float HitBoxHalfSize = 150.f;

	// The nodes that guard this HQ; they register in their BeginPlay.
	void RegisterNode(AFailoverNode* Node);
	void UnregisterNode(AFailoverNode* Node);
	const TArray<TWeakObjectPtr<AFailoverNode>>& GetNodes() const { return Nodes; }
	int32 NodesStanding() const;
	bool IsImmune() const { return HqHoldPolicy::HqImmune(GetPhase(), NodesStanding()); }
	// Joined living units in this HQ's main region: the hold's presence rule.
	HqHoldPolicy::FPresence CountPresence(const ACommandGameState& State) const;

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	// Fixture only: back to a fresh online HQ with InHealth, no hold and its emergency force unspent.
	void ResetForTest(int32 InHealth);
#endif

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
	// HqHoldPolicy::EPhase and EHoldState as bytes, and the hold's progress in seconds: replicated for the HUD.
	UPROPERTY(ReplicatedUsing = OnRep_Appearance)
	uint8 Phase = static_cast<uint8>(HqHoldPolicy::EPhase::Online);
	UPROPERTY(Replicated)
	uint8 HoldState = static_cast<uint8>(HqHoldPolicy::EHoldState::None);
	UPROPERTY(Replicated)
	float HoldProgress = 0.f;
	// Server only: progress has been above 0 since the HQ went offline.
	bool bHoldStarted = false;
	// Server only: this side's emergency force has been granted this battle.
	bool bEmergencyGranted = false;
	TArray<TWeakObjectPtr<AFailoverNode>> Nodes;
	// Local health snapshot: initial replication is not a damage event.
	bool bAudioStateInitialized = false;
	bool bDestroyedAudioPlayed = false;
	int32 LastAudioHealth = 0;
	UFUNCTION()
	void OnRep_Appearance();
	void TickHold(float DeltaSeconds);
	void GoOffline();
	void ComeBackOnline();
	void Announce(const TCHAR* OwnId, const TCHAR* EnemyId, int32 Tier) const;
	// HeadquartersEmergency.cpp: the free force of the first offline transition.
	void DeployEmergencyForces();
	void DeployHumanEmergencyForces(ACommandGameState& State);
};
