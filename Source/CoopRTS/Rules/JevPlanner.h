#pragma once

#include "CoreMinimal.h"
#include "ForceOrderPolicy.h"

namespace JevPlanner
{
constexpr float CommitmentSeconds = 25.f;
constexpr int32 MaxCandidates = 3;
// Score per Power/s of connected income an expansion or hold restores or keeps.
constexpr float ChainIncomeWeight = 4.f;
// Guarded HQs (HqHoldPolicy): a hostile Failover Node outranks other structures as a target, so the nodes fall
// before the HQ; a region holding one of the team's own nodes is worth defending beyond its other value;
// and an offline hostile HQ makes its main the objective, because presence there completes the hold.
constexpr float NodeTargetBonus = 60.f;
constexpr float NodeDefenceBonus = 40.f;
constexpr float OfflineHqScore = 300.f;

enum class EVerb : uint8
{
	MoveAndHold,
	Attack,
	Retreat
};

struct FRegion
{
	bool bExists = false;
	bool bMain = false;
	bool bClaimed = false;
	bool bAttacked = false;
	int32 Controller = INDEX_NONE;
	int32 Hostiles = 0;
	int32 DepositValue = 0;
	// Power/s of JEV's own completed Drill Rigs here, whether or not the region is connected.
	int32 IncomeValue = 0;
	// Completed Drill Rigs here that JEV does not own.
	int32 HostileRigs = 0;
	// Scales the defenders' weight in this region's score: a Fortified region of another team (FortifyPolicy).
	float DefenceMultiplier = 1.f;
	// Standing Failover Nodes of this team's own HQ in this region.
	int32 OwnNodes = 0;
	uint64 Neighbours = 0;
	FVector Position = FVector::ZeroVector;
};

// Opaque actor identity keeps structure destruction distinct from region capture.
struct FTarget
{
	uint32 Identity = 0;
	int32 Region = INDEX_NONE;
	bool bAlive = false;
	// A hostile Failover Node.
	bool bNode = false;
};

struct FWorld
{
	FRegion Regions[ForceOrders::MaxRegions];
	TConstArrayView<FTarget> Targets;
	int32 Team = 5;
	// This team's main. Without it no region is known to be connected and the chain term is off.
	int32 Home = INDEX_NONE;
	int32 EnemyHome = INDEX_NONE;
	bool bAdvantage = false;
	bool bThreatened = false;
	// A hostile Failover Node still stands: the nodes come before the HQ.
	bool bHostileNodesStand = false;
	// The hostile HQ is offline: its main is the hold objective.
	bool bHostileHqOffline = false;
};

struct FForce
{
	int32 Source = INDEX_NONE;
	int32 Home = INDEX_NONE;
	int32 UnitCount = 0;
	// The producer's configured squad size; 0 for a force no producer refills. An empty force is planned at
	// this strength, as the squad it will field once production starts (see Strength).
	int32 SquadSize = 0;
	float HealthFraction = 1.f;
	// Actual executor verb, not the stored plan: completed Retreat orders no longer qualify.
	bool bRetreating = false;
	bool bRecovering = false;
	bool bAtRecovery = false;
	// False for a force that can never refill (a free wave force): it fights on instead of recovering.
	bool bCanRefill = true;
	FVector Position = FVector::ZeroVector;
	TConstArrayView<float> ClassSpeeds;
};

struct FPlan
{
	EVerb Verb = EVerb::MoveAndHold;
	int32 Source = INDEX_NONE;
	int32 Target = INDEX_NONE;
	uint32 TargetIdentity = 0;
	int32 SizeBand = 2;
	float EtaSeconds = 0.f;
	float CommittedUntil = 0.f;
	bool bEscalated = false;
	bool bRequiresUnownedTarget = false;
	// Committed during planning, when commands are locked: the force has not been given this plan's order yet.
	bool bUnissued = false;
	// The force has had living units when this plan was made or any plan before it. An empty force with this set was
	// wiped out, not newly built.
	bool bFielded = false;
	// A wave's Attack: it outlives its commitment window. Decide keeps it while the target is valid, the force has
	// units and is not retreating, whatever the clock says and even when its source region is attacked.
	bool bStanding = false;
};

struct FCandidate
{
	FPlan Plan;
	float Score = 0.f;
};

struct FCandidates
{
	FCandidate Values[MaxCandidates];
	int32 Count = 0;
};

// Shared by commitment decisions and the command-rejection shortcut.
bool MustDefend(const FWorld& World, const FForce& Force);
// The strength a force is planned at: its living units, or, while a producer-backed force is still empty, the
// squad its producer is configured to field. Zero for an empty force nothing refills.
int32 Strength(const FForce& Force);
int32 SizeBand(int32 UnitCount);
// Regions linked to Home through regions the team controls (Home counts as controlled while
// it exists): ForceOrders::ConnectedMask, the one connectivity rule. Zero without a valid Home.
uint64 ConnectedRegions(const FWorld& World);
// Ascending-index, shortest-hop region path, matching the force order driver.
float TravelSeconds(const FWorld& World, const FForce& Force, int32 Target);
FCandidates Propose(const FWorld& World, const FForce& Force);
const FCandidate* Choose(const FCandidates& Candidates);
bool TargetValid(const FWorld& World, const FPlan& Plan);
// Returns false only when there is neither a legal proposal nor an active commitment.
// An attacked, team-controlled source forces defense unless the executor is Retreating.
// Defense is escalated at creation; escalation retains an active commitment's deadline.
// Target invalidation starts a fresh commitment. A standing plan is the exception to the clock: see FPlan::bStanding.
bool Decide(const FWorld& World, const FForce& Force, float Now, const FPlan* Current, FPlan& Out);
float Remaining(const FPlan& Plan, float Now);
}
