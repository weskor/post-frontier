#pragma once

#include "CoreMinimal.h"
#include "EnemyCommander.h"
#include "ForceOrders.h"
#include "Rules/JevExecution.h"
#include "Rules/JevPlanner.h"

class ACommandBuilding;
class ACommandGameState;
class ACommandPlayerState;
class AArmyGroup;
class AMapRegion;
class UArmyUnitDefinition;
class UMatchContent;
class ADepositSite;
class UWorld;

// Everything one evaluation pass of AEnemyCommander reads, summarises and decides.
// EnemyCommanderWorld.cpp fills it, EnemyCommanderEconomy.cpp spends from it,
// EnemyCommanderExecute.cpp turns planner choices into commands and
// EnemyCommanderPublish.cpp publishes them. `Summary.Targets` views `Targets`, so a turn never moves.
struct FJevTurn
{
	struct FDepositCandidate
	{
		ADepositSite* Deposit = nullptr;
		float Score = 0.f;
	};

	FJevTurn() = default;
	FJevTurn(const FJevTurn&) = delete;
	FJevTurn& operator=(const FJevTurn&) = delete;

	UWorld* World = nullptr;
	ACommandGameState* State = nullptr;
	ACommandPlayerState* Commander = nullptr;
	const UMatchContent* Content = nullptr;
	const UArmyUnitDefinition* Infantry = nullptr;
	int32 Team = 5;
	float Now = 0.f;
	int32 ProducerIndex = INDEX_NONE;
	int32 ExtractorIndex = INDEX_NONE;
	int32 WorkshopIndex = INDEX_NONE;
	// One full infantry squad's cost, kept unspent by every purchase.
	int32 Reserve = 0;
	FVector Home = FVector::ZeroVector;
	FVector EnemyHome = FVector::ZeroVector;
	const AMapRegion* Regions[ForceOrders::MaxRegions] = {};
	const AMapRegion* HomeRegion = nullptr;
	JevPlanner::FWorld Summary;
	TArray<ACommandBuilding*, TInlineAllocator<8>> Barracks;
	ACommandBuilding* Workshop = nullptr;
	// Configured producers per role slot.
	int32 Roles[JevExecution::RoleSlots] = {};
	TArray<AArmyGroup*, TInlineAllocator<8>> Forces;
	uint64 ForceRegions = 0;
	int32 FriendlyStrength = 0;
	int32 EnemyStrength = 0;
	// Complete, living extractors this commander owns.
	int32 Established = 0;
	TArray<FDepositCandidate, TInlineAllocator<16>> EligibleDeposits;
	TArray<JevPlanner::FTarget, TInlineAllocator<32>> Targets;
	TArray<AActor*, TInlineAllocator<32>> TargetActors;
	int32 Reservations[ForceOrders::MaxRegions] = {};
};

// Working state for one force's decide, order and publish steps.
struct FJevForceStep
{
	AArmyGroup* Force = nullptr;
	FJevCommittedForce* Current = nullptr;
	JevPlanner::FForce Snapshot;
	// Snapshot.ClassSpeeds views this value.
	float Speed = 0.f;
	JevPlanner::FPlan Next;
	bool bRecovering = false;
	bool bCommandsRejected = false;
	bool bChanged = false;
	bool bFresh = false;
	bool bNewCommitment = false;
	bool bEscalation = false;
};

inline JevPlanner::EVerb PlanVerb(EForceVerb Verb)
{
	return Verb == EForceVerb::Attack ? JevPlanner::EVerb::Attack
		: Verb == EForceVerb::Retreat ? JevPlanner::EVerb::Retreat
									  : JevPlanner::EVerb::MoveAndHold;
}

inline EForceVerb OrderVerb(JevPlanner::EVerb Verb)
{
	return Verb == JevPlanner::EVerb::Attack ? EForceVerb::Attack
		: Verb == JevPlanner::EVerb::Retreat ? EForceVerb::Retreat
											 : EForceVerb::MoveHold;
}

// The region the force is marching to, or retreating to.
int32 ActualTarget(const AArmyGroup& Force);

// World summarisation: reads the match and fills the planner's input.
namespace JevWorld
{
// False when the commander's home region cannot be identified.
bool SummariseRegions(FJevTurn& Turn);
void ScanBuildings(FJevTurn& Turn);
void ScanForces(FJevTurn& Turn);
void ScanUnits(FJevTurn& Turn);
void ScanDeposits(FJevTurn& Turn);
// Threat flag, hostile structures as targets and the advantage flag.
void Finish(FJevTurn& Turn);
}

// Economy and production decisions, placed through the command layer.
namespace JevEconomy
{
ACommandBuilding* BuildNear(const FJevTurn& Turn, int32 BuildingIndex, const FVector& Center);
void PlaceFirstProducer(const FJevTurn& Turn);
void BuildExtractor(FJevTurn& Turn);
void ConfigureProduction(FJevTurn& Turn);
void BuildNext(const FJevTurn& Turn);
}
