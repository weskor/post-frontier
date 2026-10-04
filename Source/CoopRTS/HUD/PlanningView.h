#pragma once

#include "HUDTypes.h"
#include "HUD/PlanningGhosts.h"
#include "Rules/PlanningPolicy.h"

class ACommandPlayerState;
class UBuildingDefinition;
struct FPlanningKit;

// The planning phase as the HUD and the controller read it (ui.md surface 10). Everything derives from replicated game
// state and the local commander, so drawing, hit testing and the controller's Enter agree on every peer.
namespace CommandHUDPanels
{
struct FPlanningView
{
	bool bActive = false;
	double Remaining = 0.;
	// The local commander's kit; null with none (the roster has not replicated yet).
	const FPlanningKit* Kit = nullptr;
	// Human commanders in the phase, and how many of them are Ready.
	int32 Humans = 0;
	int32 ReadyHumans = 0;
	bool bBarracksPlaced = false;
	bool bRigPlaced = false;
	// A free deposit in the team's own territory exists, so an unplaced Drill Rig has a default spot.
	bool bRigSite = false;
	// What the end of planning would still supply for the local kit.
	PlanningPolicy::FKitFill Defaults;
};
FPlanningView ReadPlanning(const FContext& Context);
// Whether a building definition is a kit piece (the Barracks or the Drill Rig); the others open at 0:00.
bool IsKitBuilding(const UBuildingDefinition& Definition);

// Where the end of planning would put the kit pieces still missing, for the dashed ghosts. Computed on the client from
// the replicated world, by the same rings and deposit rule as ACommandGameState::PlaceDefaultBarracks / PlaceDefaultRig.
FPlanningGhosts ComputeGhosts(const ACommandGameState& State, const ACommandPlayerState& Commander, const FPlanningKit& Kit);
}
