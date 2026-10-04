#pragma once

#include "CoreMinimal.h"
#include "CommandService.h"
#include "Rules/BranchPolicy.h"

class ACommandBuilding;
class ACommandPlayerState;

// Tier-2 branch purchase (Rules/BranchPolicy.h). Authoritative entry point behind UBranchCommandComponent.
struct COOPRTS_API FBranchCommands
{
	// What the verdict reads, assembled from replicated state so the panel and the authority agree on every peer.
	// Buyer may be null.
	static BranchPolicy::FInput MakeInput(const ACommandBuilding& Building, const ACommandPlayerState* Buyer);
	// Pays BranchPolicy::PowerCost and DataCost in one atomic spend and starts the building's upgrade. A rejection
	// spends nothing and carries the reason the panel shows.
	static FCommandResult Purchase(ACommandPlayerState* Buyer, ACommandBuilding* Building);
};
