#pragma once

#include "CoreMinimal.h"

namespace ArmyGroupPolicy
{
// Slot layout of one force. A produced force packs two columns around its capacity;
// an authored encounter force spreads wider and faces the other way when opposing.
struct FFormation
{
	bool bProduced = false;
	int32 Capacity = 0;
	bool bOpposing = false;
};

// Offset of a composition slot from the force's anchor. Even slots lie on -Y, odd on +Y.
FVector FormationOffset(const FFormation& Formation, int32 Slot);

// Lowest slot below Capacity whose bit is clear in Occupied, or INDEX_NONE when all are taken.
int32 FirstVacantSlot(uint32 Occupied, int32 Capacity);

// Fitting a formation inside a region. The slots of one force move together: first the whole slot set shifts
// inward (by at most MaxFitShift), then the spacing shrinks down to MinFitScale, then the set rotates (each
// turn tried from full scale down); only when nothing fits does each slot clamp to the polygon on its own.
// All steps are deterministic and stateless, so every caller that passes the same polygon and centre gets the
// same slots. The order's Destination stays the unshifted centre, and members stand at the fitted slots, so an
// arrival test must compare them with FitForce around that Destination, not with the rigid FormationOffset set.
constexpr float MinFitScale = .65f;
constexpr float MaxFitShift = 120.f;
// Clearance every slot keeps from the polygon border: the unit capsule radius plus a little.
constexpr float FitMargin = 40.f;

struct FFit
{
	FVector Centre = FVector::ZeroVector;
	float Scale = 1.f;
	// Radians, counter-clockwise seen from above.
	float Yaw = 0.f;
	// No rigid fit exists: slots clamp one by one to the polygon, at the minimum spacing.
	bool bClamped = false;
};

// Slots of the force: Capacity, or the encounter layout's six when Capacity is unset.
int32 SlotCount(const FFormation& Formation);
// Fits the slot offsets around Centre. A polygon with fewer than three points or no offsets fits as is.
FFit FitFormation(TConstArrayView<FVector2D> Polygon, const FVector& Centre, TConstArrayView<FVector> Offsets,
	float Margin = FitMargin);
// Where an offset of the unfitted layout lands under Fit; clamped fits keep every slot inside the polygon.
FVector FitPoint(TConstArrayView<FVector2D> Polygon, const FFit& Fit, const FVector& Offset);
// The fit of a whole force (every slot, occupied or not, so a recruit's slot never depends on casualties)
// and one of its slots.
FFit FitForce(const FFormation& Formation, TConstArrayView<FVector2D> Polygon, const FVector& Centre);
FVector FittedSlot(const FFormation& Formation, const FFit& Fit, TConstArrayView<FVector2D> Polygon, int32 Slot);

// A destination at a region's anchor, or within the 75 cm the capture anchor tolerates (the same limit
// the executor's near-anchor retry shifts it by), is that region's order. Any other point is a precise point.
// Region orders give a slot with no path of its own a fallback; precise points still reject obstructed formations.
constexpr float RegionAnchorTolerance = 75.f;
bool IsRegionOrderDestination(const FVector& Destination, const FVector& RegionAnchor);
// A region order's slot with no path of its own sends its unit to the nearest navigable point within this
// distance of it (or to the centre); an arrival test must allow a member that far from its fitted slot.
constexpr float SlotFallbackRadius = 300.f;

// ---- March legs: heading, column, class rows and slot assignment ----
// A march leg is one order to the next region anchor on the way. An intermediate leg of a long march ends in a
// single-file column facing the direction of travel (narrow through necks); the leg that ends in the order's
// target region ends in the box, the fitted layout above, as it always did. Hold posts keep their world-aligned
// slots. Every transform below keeps the centroid of the force's occupied slots, so the arrival test and a
// recruit's slot estimate (both of which subtract the rigid mean offset) are unaffected.
constexpr float ColumnSpacing = 80.f;
// A column's slots stay at least a capsule diameter (68 cm) plus clearance apart: a fit that would squeeze the
// file below this plans the box instead.
constexpr float ColumnFloorSpacing = 72.f;
// A leg of at least this ends in a column; a force already marching in column stays in it down to the exit.
constexpr float ColumnMinDistance = 1500.f;
constexpr float ColumnExitDistance = 1200.f;
// Heading hysteresis: a turn at most this large keeps the previous heading, and a larger one is taken only after
// the cooldown since the last heading change. A leg to a goal farther than GoalMovedDistance from the previous
// plan's is a new leg (a redirect) and takes its heading and shape fresh.
constexpr float HeadingTurnThreshold = UE_PI * 25.f / 180.f;
constexpr double HeadingCooldownSeconds = 4.;
constexpr float GoalMovedDistance = 300.f;
// Lateral jitter of a column slot, within +/-JitterRadius before the mean is removed (so within twice that after).
// It is deterministic in the force's seed and the slot, and never changes the spacing along the file.
constexpr float JitterRadius = 12.f;

enum class ELegShape : uint8
{
	Box,
	Column
};
// The shape of a leg; a force that was in column stays in it down to ColumnExitDistance.
ELegShape ChooseLegShape(bool bMarching, bool bIntermediateRegion, float DistanceToDestination, bool bWasColumn);
// Previous heading is kept for a small turn, and for a large one until the cooldown since the last turn has run.
float ChooseHeading(bool bHasPrevious, float PreviousYaw, float DesiredYaw, double SecondsSinceTurn);

// What the last plan of a force's march leg decided; every member carries a copy (server only).
struct FLegMemory
{
	bool bPlanned = false;
	bool bColumn = false;
	float Yaw = 0.f;
	// World time of the last heading change (not of the last plan).
	double TurnedAt = 0.;
	FVector2D Goal = FVector2D::ZeroVector;
};
struct FLegChoice
{
	ELegShape Shape = ELegShape::Box;
	float Yaw = 0.f;
};
// Chooses the shape and heading of a leg to Goal and updates Memory to the plan, with the hysteresis above.
FLegChoice ChooseLeg(FLegMemory& Memory, const FVector2D& Goal, float DesiredYaw, bool bMarching,
	bool bIntermediateRegion, float DistanceToDestination, double Now);
// The lateral jitter of a slot of the force with this seed, within +/-JitterRadius.
float SlotJitter(int32 Seed, int32 Index);

// Unit class rows are ranks: 0 melee front, 1 ranged middle, 2 artillery back (the caller maps its roles).
// Assigns each unit one of the slots with the least total distance, among assignments that keep the class rows:
// a unit of a lower rank never takes a slot behind (along Forward, beyond a centimetre) one of a higher rank.
// Exact (every assignment is considered, so at most MaxAssigned units); ties go to the lexicographically first.
constexpr int32 MaxAssigned = 8;
void AssignSlots(TConstArrayView<FVector2D> Positions, TConstArrayView<int32> Ranks, TConstArrayView<FVector2D> Slots,
	const FVector2D& Forward, TArray<int32, TInlineAllocator<8>>& SlotOfUnit);

struct FMarchUnit
{
	FVector2D Position = FVector2D::ZeroVector;
	int32 Rank = 1;
	// The unit's composition slot: the rigid layout slot whose offset it contributes to the occupied centroid.
	int32 Slot = 0;
};
struct FLegPlan
{
	ELegShape Shape = ELegShape::Box;
	FFit Fit;
	// One destination per input unit, in input order.
	TArray<FVector, TInlineAllocator<8>> Targets;
};
// Plans the destination slots of a leg around Centre (a box: the fitted layout of the force, its occupied slots
// reassigned; a column: a file facing Yaw about the occupied centroid, fitted inside the polygon, jittered by
// Seed). A column the fit would squeeze below ColumnFloorSpacing is planned as the box.
FLegPlan PlanLeg(const FFormation& Formation, TConstArrayView<FVector2D> Polygon, const FVector& Centre,
	TConstArrayView<FMarchUnit> Units, ELegShape Shape, float Yaw, int32 Seed);

// Team 0 forces belong to one of the five human commanders; any other force belongs to the
// enemy commander on team 5. The owner's team must also match the force's.
bool OwnerPermitted(int32 GroupTeam, int32 OwnerTeam, int32 CommanderIndex, bool bEnemyCommander);

constexpr float MaxAcquireDistance = 1450.f;

// Whether a living hostile may be engaged by a unit. Without an Attack order that is its weapon
// range. An Attack order engages targets near its anchor within MaxAcquireDistance, and, while
// marching, anything already in weapon range once the unit has left the anchor's pursuit radius.
// All distances are squared and planar. UnitToEnemy is single precision, as the unit's targeting
// has always rounded it; the anchor distances keep double precision.
struct FEngagement
{
	bool bAttackOrder = false;
	bool bMarching = false;
	float UnitToEnemy = 0.f;
	double EnemyToAnchor = 0.;
	double UnitToAnchor = 0.;
	float WeaponRange = 0.f;
	float PursuitRadius = 0.f;
};
bool EngagementPermitted(const FEngagement& Engagement);
}
