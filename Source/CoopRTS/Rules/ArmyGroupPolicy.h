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
// inward (by at most MaxFitShift, so a force that stopped at the unshifted centre still counts as arrived),
// then the spacing shrinks down to MinFitScale, then the set rotates (each turn tried from full scale down);
// only when nothing fits does each slot clamp to the polygon on its own. All steps are deterministic and
// stateless, so every caller that passes the same polygon and centre gets the same slots.
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
