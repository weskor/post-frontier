#pragma once

#include "PlanningView.h"
#include "HUDPanels.h"

class UArmyUnitDefinition;

// The planning phase's HUD (ui.md surface 10): the READY slot in Pause's place, the PLANNING panel in the deck's slot, the
// clock, the roster chips and the default-spot ghosts. Layout, hit testing and drawing read the same geometry, so a click
// always lands on what is drawn.
namespace CommandHUDPanels
{
// One chip per base unit type; the five types of the catalogue.
constexpr int32 PlanUnitCount = 5;
constexpr float PlanChipWidth = 104.f;
constexpr float PlanChipHeight = 30.f;
// The unit chips and the queue's detail text start this far in from the panel's left edge.
constexpr float PlanDetailX = 166.f;

struct FPlanningGeometry
{
	FRect Panel;
	FRect Header;
	// Place Barracks, place Drill Rig, unit type, first order.
	FRect Steps[4];
	FRect Chips[PlanUnitCount];
	FRect Clear;
	FRect Look;
	// The line of fine print left of the LOOK AT JEV BASE button.
	FRect Note;
};
FPlanningGeometry PlanningGeometry(const FRect& Panel);
// The Chip-th base unit type of the catalogue (branches are never picked); null past the last.
const UArmyUnitDefinition* PlanningUnit(const FContext& Context, int32 Chip);

bool IsPlanningAction(EHUDAction Action);
// The READY slot, and with it the panel's body, unit chips, CLEAR and LOOK AT JEV BASE.
void ForEachPlanningButton(const FContext& Context, const FLayout& Layout, TFunctionRef<void(const FButton&)> Visit);
// Draws one of those buttons; the panel's body button draws the whole panel under the rest.
void DrawPlanningButton(const FPainter& Paint, const FContext& Context, const FButton& Button, bool bHover);
// The clock slot: "PLANNING 0:47", amber, pulsing in the last 10 s.
void DrawPlanningClock(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
// The objective strip while planning: what to do, then one chip per human commander. The text stops before the TEAM opener.
void DrawPlanningStrip(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
// The dashed default-spot ghosts of the kit pieces still unplaced, on the world.
void DrawPlanningGhosts(const FPainter& Paint, const FContext& Context, const FLayout& Layout);
}
