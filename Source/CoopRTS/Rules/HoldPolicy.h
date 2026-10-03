#pragma once

#include "CoreMinimal.h"

// Region-hold decisions only; adapters supply living-unit Power and geometry.
namespace HoldPolicy
{
constexpr double CommitSeconds = 8.;
constexpr double QuietSeconds = 6.;
constexpr double ResponseRatio = 1.25;

struct FClock
{
	bool bResponding = false;
	double Started = 0.;
	double QuietSince = -1.;
};

// Returns the resulting response state. Selection never restarts an existing commitment.
bool UpdateClock(FClock& Clock, double Now, bool bAlarm, bool bSelected);

struct FCandidate
{
	double DistanceSquared = 0.;
	int32 Power = 0;
	bool bResponding = false;
	bool bSelected = false;
};

// Rewrites selection, preserving responders, then adding nearest positive-Power holders.
// Stable input order resolves ties; no allocation or sorting.
void SelectResponders(TArrayView<FCandidate> Candidates, int32 ThreatPower);
bool IsAlarmSource(bool bAlive, bool bHostile, bool bInside, bool bDamagingInside);
// Damage ceases to be current at its expiry, or when its victim/range ceases to qualify.
bool IsDamageCurrent(double Now, double Expires, bool bVictimAlive, bool bWithinWeaponRange);
bool WithinLeash(bool bInside, bool bDamagingInside, double DistanceToBorder, double WeaponRange);

// Simple polygons may be concave and have either winding; boundaries count as inside.
bool Contains(TConstArrayView<FVector2D> Polygon, const FVector2D& Point);
FVector2D ClosestBoundary(TConstArrayView<FVector2D> Polygon, const FVector2D& Point);
FVector2D ClampInside(TConstArrayView<FVector2D> Polygon, const FVector2D& Point);
// Both endpoints and every interval between boundary crossings must remain inside.
bool SegmentInside(TConstArrayView<FVector2D> Polygon, const FVector2D& Start, const FVector2D& End);

// Least occupancy wins before placement; stable input order resolves equal scores.
// Missing occupancy entries count as empty. Empty posts return INDEX_NONE.
int32 ChoosePost(TConstArrayView<FVector> Posts, TConstArrayView<int32> Occupancy,
	TConstArrayView<FVector> Assets, TConstArrayView<FVector> HostileBorders);
// Fill holes before extending a post's slots; duplicate/negative entries do not reserve extra slots.
int32 ChoosePostSlot(TConstArrayView<int32> UsedSlots);
// First occupant uses the post itself; subsequent occupants use 800 cm-spaced rings.
FVector SharedPostOffset(int32 OccupantIndex);
// Resolve clipped collisions without moving existing holders; unusable geometry keeps the clipped point.
FVector ChoosePostLocation(TConstArrayView<FVector2D> Polygon, const FVector& Post, int32 Slot,
	TConstArrayView<FVector> OccupiedLocations);
// Keep a permitted current target; otherwise take the nearest permitted target, stably.
int32 ChooseThreat(TConstArrayView<FVector> ThreatPositions, TConstArrayView<bool> Permitted,
	const FVector& ForceLocation, int32 CurrentIndex);
}
