#pragma once

#include "CoreMinimal.h"

// Where the end of planning would put the kit pieces still missing: the controller refreshes this a few times a second
// (ComputeGhosts, PlanningView.h) and the HUD draws the dashed ghosts from it.
namespace CommandHUDPanels
{
struct FPlanningGhosts
{
	bool bBarracks = false;
	FVector Barracks = FVector::ZeroVector;
	// Half the Barracks' footprint side, for the ghost's outline.
	float BarracksHalf = 0.f;
	bool bRig = false;
	FVector Rig = FVector::ZeroVector;
	float RigHalf = 0.f;
};
}
