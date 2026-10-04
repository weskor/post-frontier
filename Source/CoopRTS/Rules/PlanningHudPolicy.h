#pragma once

#include "CoreMinimal.h"

// What the planning HUD says and when Enter does what (ui.md surface 10). Pure: no actors, no world, no canvas.
// The authority is PlanningPolicy and FPlanningCommands; these are only the player-facing texts and the Enter flow.
namespace PlanningHud
{
// The clock slot pulses in the last PulseSeconds of the phase.
constexpr double PulseSeconds = 10.;
// An unplaced kit asks for a second Enter; the question and the arming lapse together (the feedback line's lifetime).
constexpr double ConfirmSeconds = 4.;

// "PLANNING 0:47": whole seconds rounded up, so the clock reads 0:00 only once the phase is over.
void AppendClock(FStringBuilderBase& Out, double Remaining);
// "0:47 left · nothing runs yet": the panel's header, on the same whole seconds.
void AppendTimeLeft(FStringBuilderBase& Out, double Remaining);
// True in the last PulseSeconds, while there is time left.
bool IsPulsing(double Remaining);
// 1 at rest; while pulsing a 1 Hz wave between .55 and 1 on Now (real seconds).
float PulseOpacity(double Remaining, double Now);

// The Pause slot while planning: "[Enter] READY (1/2)", or once Ready "✓ READY (1/2)  [Enter] Undo".
// Ready and Humans count human commanders only.
void AppendReady(FStringBuilderBase& Out, bool bReady, int32 Ready, int32 Humans);

enum class EChip : uint8
{
	Placing,
	Placed,
	Ready
};
// Ready wins; otherwise a commander with a piece still unplaced is still placing.
EChip ChipState(bool bReady, bool bKitPlaced);
// "C1 PLACING", "C1 PLACED", "C2 ✓ READY" for a zero-based commander slot.
void AppendChip(FStringBuilderBase& Out, int32 CommanderSlot, EChip State);

enum class EEnter : uint8
{
	Ignore,
	Ready,
	Unready,
	// Ask the question and arm the second press; nothing is sent.
	Confirm
};
struct FEnterInput
{
	bool bPlanning = false;
	bool bHasKit = false;
	bool bReady = false;
	// Something would be placed at a default spot (or paid back) if the commander readied now.
	bool bDefaultsNeeded = false;
	// The question was asked and has not lapsed.
	bool bConfirmLive = false;
};
// Enter outside planning, or without a kit, does nothing; Ready un-readies; an unready commander with defaults still to
// come must press twice, so a stray Enter never locks a kit the commander has not looked at.
EEnter Enter(const FEnterInput& In);
// The question stays live for ConfirmSeconds after it was asked.
bool IsConfirmLive(double Now, double Asked);
// "Barracks not placed: default spot. Press Enter again", with the other piece named too when both are missing. The names
// are the building catalogue's, so the question, the cards and the panel call a piece the same thing.
void AppendConfirm(FStringBuilderBase& Out, bool bBarracks, bool bRig, FStringView BarracksName, FStringView RigName);

// The line under a KIT card's name. Placed pieces can be moved; Ready locks both; a Drill Rig with no free deposit is
// paid back at 0:00 instead.
enum class EKitCard : uint8
{
	Free,
	Placed,
	NoDeposit,
	Locked
};
EKitCard KitCard(bool bPlaced, bool bReady, bool bSiteAvailable);
void AppendKitCard(FStringBuilderBase& Out, EKitCard State);
}
