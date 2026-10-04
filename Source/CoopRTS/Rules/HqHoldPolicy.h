#pragma once

#include "CoreMinimal.h"

// Guarded HQs (decisions H1-H4, Design/battle.md "Guarding the HQs"): Failover Nodes, the offline hold,
// the emergency wave and the HQ's lifecycle. Rules only: the HQ, the node actor, the HUD and the
// harness read these, never copies.
namespace HqHoldPolicy
{
// Failover Nodes: two per HQ, 1000 HP, 90% less damage until v1.2 (240 s of battle time).
inline constexpr int32 NodesPerHq = 2;
inline constexpr int32 NodeHealth = 1000;
// One entry of the DamagePolicy incoming list, taken by a node while it is plated.
inline constexpr float PlatingIncomingMultiplier = .1f;
inline constexpr float PlatingEndSeconds = 240.f;
// Seconds of uninterrupted attacker progress that complete the hold and lose the battle for the HQ's side.
inline constexpr float HoldSeconds = 75.f;
// An HQ that comes back online restores this fraction of its maximum HP.
inline constexpr float RestoreFraction = .25f;

// The HQ's lifecycle, the single source for "is this side still in the battle".
enum class EPhase : uint8
{
	Online,
	// At 0 HP: the main is a hold objective, and stays controlled and connected for its owner.
	Offline,
	// The hold completed: the battle is lost for the HQ's side.
	Lost
};

// What the hold is doing right now; drives the three state lines of the hold bar.
enum class EHoldState : uint8
{
	// Not offline: there is no hold.
	None,
	// Attackers present and no defenders: progress grows.
	Holding,
	// Any defender present: progress stays.
	Paused,
	// Nobody present: progress decays at the same rate it grows.
	Decaying
};

// The hold of one offline HQ. Progress is in seconds of uninterrupted attacker presence, 0 to HoldSeconds.
struct FHold
{
	float Progress = 0.f;
	// Progress has been above 0 since the HQ went offline; only then does reaching 0 bring it back online.
	bool bStarted = false;
};

// Joined living units standing in the main region's polygon, per side.
struct FPresence
{
	int32 Attackers = 0;
	int32 Defenders = 0;
};

struct FStep
{
	EPhase Phase = EPhase::Online;
	FHold Hold;
	EHoldState State = EHoldState::None;
	// The HQ came back online this step: it restores RestoredHealth.
	bool bRevived = false;
	// The hold completed this step.
	bool bCompleted = false;
};

// Advances an HQ's phase by DeltaSeconds. Online and Lost HQs do not change and report no hold.
// Offline: attackers with no defender add DeltaSeconds; any defender pauses; nobody decays by DeltaSeconds.
// Full progress completes the hold (Lost); decay back to 0 after progress existed revives (Online, fresh hold).
FStep Advance(EPhase Phase, const FHold& Hold, const FPresence& Presence, float DeltaSeconds);
// Hold progress as a fraction of the full hold, 0 to 1.
float Fraction(const FHold& Hold);
// Whole seconds of progress for the timer text (rounded down, never above HoldSeconds).
int32 WholeSeconds(const FHold& Hold);

// The HQ takes no damage while either node stands, and the immunity is meaningful only while it is online.
bool HqImmune(EPhase Phase, int32 NodesStanding);
// A node is plated (takes PlatingIncomingMultiplier) before PlatingEndSeconds of battle time.
bool NodePlated(float BattleSeconds);
// HP an HQ restores when it comes back online.
int32 RestoredHealth(int32 MaxHealth);
// The emergency force is granted the first time a side's HQ goes offline, once per battle per side.
// Returns true exactly once for each flag; the flag records the grant.
bool ClaimEmergency(bool& bGranted);
}
