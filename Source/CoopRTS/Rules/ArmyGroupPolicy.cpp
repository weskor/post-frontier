#include "Rules/ArmyGroupPolicy.h"

#include "Rules/GameplayConstants.h"
#include "Rules/HoldPolicy.h"

namespace
{
using ArmyGroupPolicy::MinFitScale;

// Spacing steps tried before rotating; the last is the floor.
constexpr float FitScales[] = { 1.f, .9f, .8f, .7f, MinFitScale };
static_assert(MinFitScale < .7f, "The floor must be the smallest step");
constexpr float YawStep = UE_PI / 12.f;
constexpr int32 MaxTurns = 6;
constexpr int32 MaxShiftSteps = 16;
// Corrections below this (cm) count as satisfied, so a point placed at the margin is not pushed again.
constexpr double FitTolerance = .05;

using FSlots = TArray<FVector2D, TInlineAllocator<8>>;

FVector2D Transform(const FVector& Offset, float Scale, float Yaw)
{
	const double Cos = FMath::Cos(Yaw), Sin = FMath::Sin(Yaw);
	return FVector2D((Offset.X * Cos - Offset.Y * Sin) * Scale, (Offset.X * Sin + Offset.Y * Cos) * Scale);
}

// The unit normal of the polygon edge nearest to Point, pointing into the polygon; zero for a degenerate polygon.
FVector2D InwardNormal(TConstArrayView<FVector2D> Polygon, const FVector2D& Point)
{
	double Nearest = TNumericLimits<double>::Max();
	FVector2D Normal = FVector2D::ZeroVector;
	for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
	{
		const FVector2D Edge = Polygon[Index] - Polygon[Previous];
		const double Length = Edge.Size();
		if (Length == 0.)
			continue;
		const double Along = FMath::Clamp(FVector2D::DotProduct(Point - Polygon[Previous], Edge) / (Length * Length), 0., 1.);
		const double Distance = FVector2D::DistSquared(Point, Polygon[Previous] + Edge * Along);
		if (Distance < Nearest)
		{
			Nearest = Distance;
			Normal = FVector2D(-Edge.Y, Edge.X) / Length;
		}
	}
	// The side that is inside, probed a centimetre off the edge (either winding).
	const FVector2D Boundary = HoldPolicy::ClosestBoundary(Polygon, Point);
	return HoldPolicy::Contains(Polygon, Boundary + Normal) ? Normal : -Normal;
}

// The displacement that brings Point inside Polygon with Margin to spare; zero when it already is.
FVector2D InwardCorrection(TConstArrayView<FVector2D> Polygon, const FVector2D& Point, double Margin)
{
	const FVector2D Boundary = HoldPolicy::ClosestBoundary(Polygon, Point);
	if (!HoldPolicy::Contains(Polygon, Point))
		return (Boundary - Point) + InwardNormal(Polygon, Point) * Margin;
	const double Clearance = FVector2D::Distance(Point, Boundary);
	if (Margin - Clearance <= FitTolerance)
		return FVector2D::ZeroVector;
	// Straight away from the nearest border, or along its normal when standing exactly on it.
	const FVector2D Away = Clearance > FitTolerance ? (Point - Boundary) / Clearance : InwardNormal(Polygon, Point);
	return Away * (Margin - Clearance);
}

// Moves the centre by the largest outstanding correction until every slot is inside with its margin, or
// the centre has left its allowance.
bool ShiftInside(TConstArrayView<FVector2D> Polygon, const FSlots& Slots, const FVector2D& Centre, double Margin,
	FVector2D& Shifted)
{
	FVector2D Moved = Centre;
	for (int32 Step = 0; Step < MaxShiftSteps; ++Step)
	{
		FVector2D Worst = FVector2D::ZeroVector;
		for (const FVector2D& Slot : Slots)
		{
			const FVector2D Correction = InwardCorrection(Polygon, Moved + Slot, Margin);
			if (Correction.SizeSquared() > Worst.SizeSquared())
				Worst = Correction;
		}
		if (Worst.IsZero())
		{
			Shifted = Moved;
			return true;
		}
		Moved += Worst;
		if (FVector2D::DistSquared(Moved, Centre) > FMath::Square(static_cast<double>(ArmyGroupPolicy::MaxFitShift)))
			return false;
	}
	return false;
}

bool TryFit(TConstArrayView<FVector2D> Polygon, const FVector2D& Centre, TConstArrayView<FVector> Offsets,
	float Scale, float Yaw, double Margin, FVector2D& Shifted)
{
	FSlots Slots;
	for (const FVector& Offset : Offsets)
		Slots.Add(Transform(Offset, Scale, Yaw));
	return ShiftInside(Polygon, Slots, Centre, Margin, Shifted);
}
}

namespace ArmyGroupPolicy
{
FVector FormationOffset(const FFormation& Formation, int32 Slot)
{
	const float Side = Slot % 2 ? 1.f : -1.f;
	if (Formation.bProduced)
		return FVector((Formation.Capacity / 2 - 1 - 2 * (Slot / 2)) * (GameplayConstants::FormationSpacing * .5f),
			Side * (GameplayConstants::FormationSpacing * .5f), 0.f);
	return FVector((Formation.bOpposing ? -1.f : 1.f) * (1 - Slot / 2) * 220.f, Side * 140.f, 0.f);
}

int32 FirstVacantSlot(uint32 Occupied, int32 Capacity)
{
	int32 Slot = 0;
	while (Slot < Capacity && (Occupied & (1u << Slot)))
		++Slot;
	return Slot == Capacity ? INDEX_NONE : Slot;
}

int32 SlotCount(const FFormation& Formation)
{
	return Formation.Capacity > 0 ? Formation.Capacity : 6;
}

FFit FitFormation(TConstArrayView<FVector2D> Polygon, const FVector& Centre, TConstArrayView<FVector> Offsets,
	float Margin)
{
	FFit Fit;
	Fit.Centre = Centre;
	if (Polygon.Num() < 3 || Offsets.IsEmpty())
		return Fit;
	FVector2D Shifted;
	const auto Accept = [&](float Scale, float Yaw) {
		if (!TryFit(Polygon, FVector2D(Centre), Offsets, Scale, Yaw, Margin, Shifted))
			return false;
		Fit.Centre = FVector(Shifted.X, Shifted.Y, Centre.Z);
		Fit.Scale = Scale;
		Fit.Yaw = Yaw;
		return true;
	};
	for (const float Scale : FitScales)
		if (Accept(Scale, 0.f))
			return Fit;
	// Each turn, positive first, is tried from full spacing down: a turned formation at full spacing beats a
	// squeezed one that did not need to turn.
	for (int32 Turn = 1; Turn <= MaxTurns; ++Turn)
		for (const float Sign : { 1.f, -1.f })
			for (const float Scale : FitScales)
				if (Accept(Scale, Sign * Turn * YawStep))
					return Fit;
	Fit.Centre = Centre;
	Fit.Scale = MinFitScale;
	Fit.Yaw = 0.f;
	Fit.bClamped = true;
	return Fit;
}

FVector FitPoint(TConstArrayView<FVector2D> Polygon, const FFit& Fit, const FVector& Offset)
{
	FVector2D Point = FVector2D(Fit.Centre) + Transform(Offset, Fit.Scale, Fit.Yaw);
	if (Fit.bClamped && Polygon.Num() >= 3)
		Point = HoldPolicy::ClampInside(Polygon, Point);
	return FVector(Point.X, Point.Y, Fit.Centre.Z);
}

FFit FitForce(const FFormation& Formation, TConstArrayView<FVector2D> Polygon, const FVector& Centre)
{
	TArray<FVector, TInlineAllocator<8>> Offsets;
	for (int32 Slot = 0; Slot < SlotCount(Formation); ++Slot)
		Offsets.Add(FormationOffset(Formation, Slot));
	return FitFormation(Polygon, Centre, Offsets);
}

FVector FittedSlot(const FFormation& Formation, const FFit& Fit, TConstArrayView<FVector2D> Polygon, int32 Slot)
{
	return FitPoint(Polygon, Fit, FormationOffset(Formation, Slot));
}

bool IsRegionOrderDestination(const FVector& Destination, const FVector& RegionAnchor)
{
	return FVector::DistSquared2D(Destination, RegionAnchor) <= FMath::Square(static_cast<double>(RegionAnchorTolerance));
}

bool OwnerPermitted(int32 GroupTeam, int32 OwnerTeam, int32 CommanderIndex, bool bEnemyCommander)
{
	if (OwnerTeam != GroupTeam)
		return false;
	return GroupTeam == 0 ? CommanderIndex >= 0 && CommanderIndex < 5 : GroupTeam == 5 && bEnemyCommander;
}

bool EngagementPermitted(const FEngagement& Engagement)
{
	const float RangeSquared = FMath::Square(Engagement.WeaponRange);
	if (!Engagement.bAttackOrder)
		return Engagement.UnitToEnemy <= RangeSquared;
	const float RadiusSquared = FMath::Square(Engagement.PursuitRadius);
	const bool bNearAnchor = Engagement.EnemyToAnchor <= RadiusSquared && Engagement.UnitToAnchor <= RadiusSquared;
	const bool bEnRoute = Engagement.bMarching && Engagement.UnitToAnchor > RadiusSquared
		&& Engagement.UnitToEnemy <= RangeSquared;
	return (bNearAnchor && Engagement.UnitToEnemy <= FMath::Square(MaxAcquireDistance)) || bEnRoute;
}
}
