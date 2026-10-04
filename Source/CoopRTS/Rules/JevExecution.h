#pragma once

#include "CoreMinimal.h"
#include "JevPlanner.h"
#include "JevReleasePolicy.h"

// Pure decisions the enemy commander's executor makes around a planner choice:
// what to claim, when an order or its publication changes, and what to build.
namespace JevExecution
{
// A displayed retreat arrives faster than the planner's march estimate.
constexpr float RetreatSpeedFactor = 1.25f;
constexpr float RecoveryEnterHealth = .35f;
constexpr float RecoveryExitHealth = .8f;
constexpr float DepositRateWeight = 3.f;
constexpr float DepositDistanceScale = 2000.f;
constexpr int32 MaxProducers = 3;
// Production role slots, in EUnitRole order: 0 Frontline, 1 Ranged, 2 Siege, 3 Assault, 4 Support.
constexpr int32 RoleSlots = 5;
// The slots below this one are the base roles every JEV army fills first.
constexpr int32 BaseRoleSlots = 3;
constexpr int32 AssaultSlot = 3;
constexpr int32 SupportSlot = 4;

inline bool ValidRegion(int32 Index) { return Index >= 0 && Index < ForceOrders::MaxRegions; }

// The role slot a new producer takes. The first producer is Frontline. After that the humans' most
// numerous armor class decides: Light is answered by Frontline, Heavy by Ranged then Assault (Piercing),
// Shielded by Support (EMP), each only while it has no producer. Otherwise the first base role with no
// producer; once all three exist, Frontline unless it outnumbers Ranged.
int32 NextRoleSlot(const int32 (&Counts)[RoleSlots], const JevRelease::FArmorCounts& Humans);
// One living unit's hit points and shield.
struct FUnitHealth
{
	int32 Health = 0;
	int32 MaxHealth = 0;
	int32 Shield = 0;
	int32 MaxShield = 0;
};
// A force's mean unit health, shield counted with hit points: (HP + shield) / (max HP + max shield) per
// unit. 1 for no units, so an empty force never reads as hurt.
float HealthFraction(TConstArrayView<FUnitHealth> Units);
// A squad's worth of units, a 5:4 strength edge and no income deficit.
bool HasAdvantage(int32 Friendly, int32 Enemy, int32 SquadSize, int32 FriendlyIncome, int32 EnemyIncome);
// Recovery starts below RecoveryEnterHealth and holds until RecoveryExitHealth.
bool Recovering(float HealthFraction, bool bWasRecovering);
// True when a team-controlled region has hostiles inside or is being damaged.
bool IsThreatened(const JevPlanner::FWorld& World);
float DepositScore(int32 RatePerSecond, double DistanceToHome);
bool CanAfford(int32 Resources, int32 Cost, int32 Reserve);

// An unexpired, still-valid commitment to an unowned region claims it for that force.
bool HoldsClaim(const JevPlanner::FWorld& World, const JevPlanner::FPlan& Plan, float Now);
// A plan the force just committed to claims its unowned, non-Retreat target.
bool ClaimsTarget(const JevPlanner::FWorld& World, const JevPlanner::FPlan& Plan);

// The plan describing the order the force is executing, keeping any live commitment deadline.
JevPlanner::FPlan ActualPlan(const JevPlanner::FWorld& World, const JevPlanner::FForce& Force,
	const JevPlanner::FPlan* Current, float Now, JevPlanner::EVerb ActualVerb, int32 ActualTarget,
	uint32 TargetIdentity);
// The executor's current order shown to the player without replacing the strategic ticket.
JevPlanner::FPlan DisplayPlan(const JevPlanner::FWorld& World, const JevPlanner::FForce& Force,
	const JevPlanner::FPlan& Next, JevPlanner::EVerb ActualVerb, int32 ActualTarget, bool bActualHolding);
// A freshly issued Retreat reports the region the force actually chose.
void AdoptRetreatRegion(const JevPlanner::FWorld& World, const JevPlanner::FForce& Force, int32 RetreatRegion,
	JevPlanner::FPlan& Plan);
bool IsEscalation(const JevPlanner::FPlan* Current, const JevPlanner::FPlan& Next);

struct FOrderChange
{
	bool bFresh = false;
	bool bChanged = false;
};
// A new commitment or a changed decision reissues an order only if the force's actual order differs.
FOrderChange OrderChange(const JevPlanner::FPlan& Next, const JevPlanner::FPlan* Current, bool bActualDiffers);
// A decision that carries a different commitment deadline than the one on record starts a new ticket.
bool NewCommitment(const JevPlanner::FPlan* Current, const JevPlanner::FPlan& Next);

struct FPublished
{
	int32 Ticket = 0;
	JevPlanner::EVerb Verb = JevPlanner::EVerb::MoveAndHold;
	int32 Target = INDEX_NONE;
	float EtaSeconds = 0.f;
	int32 SizeBand = 0;
	bool bEscalated = false;
};
struct FPublicationChange
{
	bool bEtaRestarted = false;
	bool bMemoChanged = false;
};
// The ETA is a duration from the moment it was computed; a size-band change alone keeps it.
FPublicationChange PublicationChange(const FPublished* Existing, int32 Ticket, const JevPlanner::FPlan& Display);

// Nearest-to-enemy controlled, uncontested non-main region without a producer; INDEX_NONE if none.
int32 ForwardRegion(const JevPlanner::FWorld& World, const FVector& EnemyHome, uint64 Contested, uint64 Producers);

enum class EEconomyAction : uint8
{
	None,
	BuildProducer,
	BuildWorkshop,
	Research
};
struct FEconomy
{
	bool bThreatened = false;
	bool bForwardAnchor = false;
	bool bHasWorkshop = false;
	bool bWorkshopComplete = false;
	bool bWorkshopDefined = false;
	bool bDoctrineChosen = false;
	int32 Established = 0;
	int32 Producers = 0;
	int32 Resources = 0;
	int32 Reserve = 0;
	int32 ProducerCost = 0;
	int32 WorkshopCost = 0;
	int32 ResearchCost = 0;
};
// Producer first, else workshop, else research; each keeps a reserve for one full squad.
EEconomyAction NextEconomyAction(const FEconomy& Economy);
}
