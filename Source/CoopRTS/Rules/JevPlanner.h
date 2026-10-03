#pragma once

#include "CoreMinimal.h"
#include "ForceOrderPolicy.h"

namespace JevPlanner
{
constexpr float CommitmentSeconds = 25.f;
constexpr int32 MaxCandidates = 3;

enum class EVerb : uint8
{
	MoveAndHold,
	Attack,
	Retreat
};

struct FRegion
{
	bool bExists = false;
	bool bTargetAlive = true;
	bool bMain = false;
	bool bClaimed = false;
	bool bAttacked = false;
	int32 Controller = INDEX_NONE;
	int32 Hostiles = 0;
	int32 DepositValue = 0;
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
	bool bRecovering = false;
	bool bAtRecovery = false;
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

int32 SizeBand(int32 UnitCount);
// Ascending-index, shortest-hop region path, matching the force order driver.
float TravelSeconds(const FWorld& World, const FForce& Force, int32 Target);
FCandidates Propose(const FWorld& World, const FForce& Force);
const FCandidate* Choose(const FCandidates& Candidates);
bool TargetValid(const FWorld& World, const FPlan& Plan);
// Returns false only when there is neither a legal proposal nor an active commitment.
// Escalation retains the original deadline; invalidation starts a fresh commitment.
bool Decide(const FWorld& World, const FForce& Force, float Now, const FPlan* Current, FPlan& Out);
float Remaining(const FPlan& Plan, float Now);
}
