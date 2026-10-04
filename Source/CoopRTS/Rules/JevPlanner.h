#pragma once

#include "CoreMinimal.h"
#include "ForceOrderPolicy.h"

namespace JevPlanner
{
constexpr float CommitmentSeconds = 25.f;
constexpr int32 MaxCandidates = 3;
// Score per Power/s of connected income an expansion or hold restores or keeps.
constexpr float ChainIncomeWeight = 4.f;

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
	uint64 Neighbours = 0;
	FVector Position = FVector::ZeroVector;
};

// Opaque actor identity keeps structure destruction distinct from region capture.
struct FTarget
{
	uint32 Identity = 0;
	int32 Region = INDEX_NONE;
	bool bAlive = false;
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
};

struct FForce
{
	int32 Source = INDEX_NONE;
	int32 Home = INDEX_NONE;
	int32 UnitCount = 0;
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
// Target invalidation starts a fresh commitment.
bool Decide(const FWorld& World, const FForce& Force, float Now, const FPlan* Current, FPlan& Out);
float Remaining(const FPlan& Plan, float Now);
}
