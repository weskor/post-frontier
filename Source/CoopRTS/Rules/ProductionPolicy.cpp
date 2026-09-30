#include "Rules/ProductionPolicy.h"

namespace
{
	EProductionState Classify(const FProductionInput& In)
	{
		if (!In.bMatchOngoing) return EProductionState::MatchFinished;
		if (!In.bProducer || !In.bAlive) return EProductionState::NotProducer;
		if (!In.bComplete) return EProductionState::UnderConstruction;
		if (!In.bConfigured) return EProductionState::Unconfigured;
		if (!In.bForceValid) return EProductionState::ForceUnavailable;
		if (!In.bEnabled) return EProductionState::Paused;
		if (In.Joined + In.Travelling >= In.Capacity) return EProductionState::ForceComplete;
		if (!In.bWalletValid) return EProductionState::WalletUnavailable;
		if (In.Balance < In.UnitCost) return EProductionState::InsufficientResources;
		if (In.Progress >= In.Duration) return EProductionState::DeploymentBlocked;
		return EProductionState::Producing;
	}
}

FProductionDecision ProductionPolicy::Evaluate(const FProductionInput& In)
{
	FProductionDecision Out{Classify(In), In.Progress, false};
	if (Out.State == EProductionState::Producing && In.DeltaSeconds > 0.f)
		Out.NewProgress = FMath::Min(In.Duration, In.Progress + In.DeltaSeconds);
	// Completed work is only retried while the building is otherwise able to produce;
	// pause, a full force or an empty wallet hold it without attempting deployment.
	Out.bDeploymentDue = (Out.State == EProductionState::Producing || Out.State == EProductionState::DeploymentBlocked)
		&& Out.NewProgress >= In.Duration;
	return Out;
}
