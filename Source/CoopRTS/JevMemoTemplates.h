#pragma once

#include "CoreMinimal.h"

namespace JevPlanner
{
struct FPlan;
}

class FJevMemoTemplates
{
public:
	// Loads [JevMemos] from the packaged Game config; invalid data fails without a fallback.
	bool Load();
	// RegionName is the display name of Plan.Target. ETA is rounded up to whole seconds.
	// Returns empty if templates have not loaded or the plan cannot be represented faithfully.
	FString Format(const JevPlanner::FPlan& Plan, int32 TicketNumber, const FString& RegionName) const;

	// The memo of a published Split-Brain Cut plan, from the SplitBrainCut template. Same rounding and empty result.
	FString FormatCut(int32 TicketNumber, int32 SizeBand, float EtaSeconds, const FString& RegionName) const;

private:
	FString Templates[5];
	bool bLoaded = false;
};
