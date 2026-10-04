#pragma once

#include "CoreMinimal.h"
#include "Rules/FortifyPolicy.h"

// What a Fortify cast at the cursor would do: the region it lands in and the rules' verdict for it.
struct FFortifyPreview
{
	int32 RegionIndex = INDEX_NONE;
	FString RegionName;
	FortifyPolicy::FDecision Decision;
	bool IsAllowed() const { return Decision.IsAccepted(); }
};
