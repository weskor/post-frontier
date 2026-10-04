#pragma once

#include "CoreMinimal.h"

// The planning phase before 0:00 (decision O1): timing, roster and kit rules. Pure: no actors, no world.
namespace PlanningPolicy
{
// Real seconds the phase lasts at most; it ends sooner when every human commander is Ready.
constexpr double PlanningSeconds = 60.;

enum class EEnd : uint8
{
	Continue,
	AllReady,
	Expired
};

// Never negative.
double Remaining(double Now, double Deadline);
// Planning ends the moment the last human is Ready, or at the deadline. With no human on the roster it
// cannot end early (the host may not have joined yet) but still expires. Expiry wins a tie.
EEnd Evaluate(int32 Humans, int32 ReadyHumans, double Now, double Deadline);

// Kits stand on the navmesh, so the phase waits for it; the wait ends this long after the deadline whatever the
// navigation system reports.
constexpr double NavigationGraceSeconds = 5.;
bool MayEnd(bool bNavigationReady, double Now, double Deadline);

struct FRosterChange
{
	TArray<int32, TInlineAllocator<5>> Join;
	TArray<int32, TInlineAllocator<5>> Leave;
};
// Commander slots that gained a kit and slots whose kit must go; both ascending, duplicates ignored.
FRosterChange Reconcile(TConstArrayView<int32> KitSlots, TConstArrayView<int32> Roster);

// Why a kit edit (placement, move, unit type, first order) is refused, or null when it is allowed.
// Edits need an active phase and a commander who is not Ready.
const TCHAR* EditRejection(bool bPlanning, bool bReady);

struct FRigSite
{
	FVector Position = FVector::ZeroVector;
	// No extractor reserves it and ore remains.
	bool bFree = false;
	// Its region belongs to the commander's team and is uncontested.
	bool bOwnTerritory = false;
};
// The free, own-territory deposit nearest From in the plane, the earliest on ties; INDEX_NONE when there is none.
int32 NearestRigSite(TConstArrayView<FRigSite> Sites, const FVector& From);

struct FKitFill
{
	bool bPlaceBarracks = false;
	bool bPlaceRig = false;
	// No free deposit: the Drill Rig's cost in Power instead of the Rig.
	bool bRefundRig = false;
};
// What expiry or the end of planning must still supply for one commander's kit.
FKitFill Fill(bool bBarracksPlaced, bool bRigPlaced, bool bRigSiteAvailable);

// The default Barracks spots, in the order the end of planning tries them: 9 rings of 32 directions around the home
// headquarters, 380 uu out and 160 uu more per ring, mirrored for the far side (team 5). The first legal spot clear of free
// deposits wins (GameStatePlanning::FindDefaultBarracks, which the server and the HUD's ghost both call).
constexpr int32 DefaultSpotCount = 9 * 32;
FVector DefaultBarracksSpot(const FVector& Home, int32 Team, int32 Index);
}
