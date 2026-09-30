#pragma once

#include "CoreMinimal.h"
#include "ProductionPolicy.generated.h"

UENUM(BlueprintType)
enum class EProductionState : uint8
{
	MatchFinished, NotProducer, UnderConstruction, Unconfigured, ForceUnavailable,
	Paused, ForceComplete, WalletUnavailable, InsufficientResources, DeploymentBlocked, Producing
};

struct FProductionInput
{
	bool bMatchOngoing, bProducer, bComplete, bAlive, bConfigured, bForceValid, bEnabled, bWalletValid;
	int32 Joined, Travelling, Capacity, Balance, UnitCost;
	float Progress, Duration, DeltaSeconds;
};

struct FProductionDecision
{
	EProductionState State;
	float NewProgress;      // progress after DeltaSeconds; unchanged unless State == Producing
	bool bDeploymentDue;    // NewProgress >= Duration and the adapter should attempt one deployment
};

namespace ProductionPolicy
{
	// Pure: no world, no actors. Precedence is fixed; earlier states mask later ones.
	FProductionDecision Evaluate(const FProductionInput& In);
}
