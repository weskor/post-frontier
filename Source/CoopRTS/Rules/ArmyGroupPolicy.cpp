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

namespace
{
constexpr double RowTolerance = 1.;
constexpr double CostEpsilon = 1.e-6;

uint32 Mix(uint32 Value)
{
	Value ^= Value >> 16;
	Value *= 0x7feb352du;
	Value ^= Value >> 15;
	Value *= 0x846ca68bu;
	return Value ^ (Value >> 16);
}

// Depth-first over the assignments in lexicographic order; a later assignment must beat the best by an epsilon,
// so the first of equal cost wins.
struct FAssignment
{
	TConstArrayView<FVector2D> Positions;
	TConstArrayView<int32> Ranks;
	TConstArrayView<FVector2D> Slots;
	TArray<double, TInlineAllocator<8>> Forwardness;
	TArray<int32, TInlineAllocator<8>> Current, Best;
	uint32 Used = 0;
	double BestCost = TNumericLimits<double>::Max();

	bool KeepsRows(int32 Unit, int32 Slot) const
	{
		for (int32 Other = 0; Other < Unit; ++Other)
		{
			const double Held = Forwardness[Current[Other]], Taken = Forwardness[Slot];
			if ((Ranks[Other] < Ranks[Unit] && Held < Taken - RowTolerance)
				|| (Ranks[Other] > Ranks[Unit] && Taken < Held - RowTolerance))
				return false;
		}
		return true;
	}

	void Search(int32 Unit, double Cost)
	{
		if (Cost >= BestCost - CostEpsilon)
			return;
		if (Unit == Positions.Num())
		{
			BestCost = Cost;
			Best = Current;
			return;
		}
		for (int32 Slot = 0; Slot < Slots.Num(); ++Slot)
		{
			if ((Used & (1u << Slot)) || !KeepsRows(Unit, Slot))
				continue;
			Current[Unit] = Slot;
			Used |= 1u << Slot;
			Search(Unit + 1, Cost + FVector2D::Distance(Positions[Unit], Slots[Slot]));
			Used &= ~(1u << Slot);
		}
	}
};

// The file of a column: Count slots along Yaw, front first, centred on the origin, with lateral jitter whose mean
// is removed. The spacing along the file is exactly ColumnSpacing.
void ColumnOffsets(int32 Count, float Yaw, int32 Seed, TArray<FVector, TInlineAllocator<8>>& Offsets)
{
	const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw)), Left(-Forward.Y, Forward.X);
	double MeanJitter = 0.;
	for (int32 Index = 0; Index < Count; ++Index)
		MeanJitter += ArmyGroupPolicy::SlotJitter(Seed, Index) / Count;
	for (int32 Index = 0; Index < Count; ++Index)
	{
		const FVector2D Place = Forward * ((Count - 1) * .5 - Index) * ArmyGroupPolicy::ColumnSpacing
			+ Left * (ArmyGroupPolicy::SlotJitter(Seed, Index) - MeanJitter);
		Offsets.Add(FVector(Place.X, Place.Y, 0.));
	}
}
}

ELegShape ChooseLegShape(bool bMarching, bool bIntermediateRegion, float DistanceToDestination, bool bWasColumn)
{
	const float Needed = bWasColumn ? ColumnExitDistance : ColumnMinDistance;
	return bMarching && bIntermediateRegion && DistanceToDestination >= Needed ? ELegShape::Column : ELegShape::Box;
}

float ChooseHeading(bool bHasPrevious, float PreviousYaw, float DesiredYaw, double SecondsSinceTurn)
{
	if (!bHasPrevious)
		return DesiredYaw;
	const float Turn = FMath::Abs(FMath::FindDeltaAngleRadians(PreviousYaw, DesiredYaw));
	return Turn <= HeadingTurnThreshold || SecondsSinceTurn < HeadingCooldownSeconds ? PreviousYaw : DesiredYaw;
}

FLegChoice ChooseLeg(FLegMemory& Memory, const FVector2D& Goal, float DesiredYaw, bool bMarching,
	bool bIntermediateRegion, float DistanceToDestination, double Now)
{
	const bool bSameLeg = Memory.bPlanned && FVector2D::Distance(Memory.Goal, Goal) <= GoalMovedDistance;
	FLegChoice Choice;
	Choice.Yaw = bSameLeg ? ChooseHeading(true, Memory.Yaw, DesiredYaw, Now - Memory.TurnedAt) : DesiredYaw;
	Choice.Shape = ChooseLegShape(bMarching, bIntermediateRegion, DistanceToDestination, bSameLeg && Memory.bColumn);
	if (!bSameLeg || Choice.Yaw != Memory.Yaw)
		Memory.TurnedAt = Now;
	Memory.bPlanned = true;
	Memory.bColumn = Choice.Shape == ELegShape::Column;
	Memory.Yaw = Choice.Yaw;
	Memory.Goal = Goal;
	return Choice;
}

float SlotJitter(int32 Seed, int32 Index)
{
	const uint32 Bits = Mix(Mix(static_cast<uint32>(Seed)) ^ (static_cast<uint32>(Index) * 2u + 1u));
	return (static_cast<float>((Bits >> 8) & 0xffffu) / 32767.5f - 1.f) * JitterRadius;
}

FVector2D ColumnTail(TConstArrayView<FVector2D> Targets, float Yaw)
{
	if (Targets.IsEmpty())
		return FVector2D::ZeroVector;
	const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
	FVector2D Rear = Targets[0];
	for (const FVector2D& Target : Targets)
		if (FVector2D::DotProduct(Target, Forward) < FVector2D::DotProduct(Rear, Forward))
			Rear = Target;
	return Rear - Forward * ColumnSpacing;
}

void AssignSlots(TConstArrayView<FVector2D> Positions, TConstArrayView<int32> Ranks, TConstArrayView<FVector2D> Slots,
	const FVector2D& Forward, TArray<int32, TInlineAllocator<8>>& SlotOfUnit)
{
	const int32 Count = Positions.Num();
	SlotOfUnit.Reset();
	if (Count > MaxAssigned || Slots.Num() < Count || Ranks.Num() < Count)
	{
		for (int32 Index = 0; Index < Count; ++Index)
			SlotOfUnit.Add(Index);
		return;
	}
	FAssignment Assignment{ Positions, Ranks, Slots };
	for (const FVector2D& Slot : Slots)
		Assignment.Forwardness.Add(FVector2D::DotProduct(Slot, Forward));
	Assignment.Current.Init(0, Count);
	Assignment.Search(0, 0.);
	SlotOfUnit = Assignment.Best;
	for (int32 Index = SlotOfUnit.Num(); Index < Count; ++Index)
		SlotOfUnit.Add(Index);
}

namespace
{
// The points a column of Units takes around Centre: a file centred on the occupied slots' centroid, so the
// force's mean stays where the box put it.
FFit ColumnPoints(const FFormation& Formation, TConstArrayView<FVector2D> Polygon, const FVector& Centre,
	TConstArrayView<ArmyGroupPolicy::FMarchUnit> Units, float Yaw, int32 Seed, TArray<FVector, TInlineAllocator<8>>& Points)
{
	FVector Centroid = FVector::ZeroVector;
	for (const ArmyGroupPolicy::FMarchUnit& Unit : Units)
		Centroid += ArmyGroupPolicy::FormationOffset(Formation, Unit.Slot) / Units.Num();
	TArray<FVector, TInlineAllocator<8>> Offsets;
	ColumnOffsets(Units.Num(), Yaw, Seed, Offsets);
	const FFit Fit = ArmyGroupPolicy::FitFormation(Polygon, Centre + Centroid, Offsets);
	for (const FVector& Offset : Offsets)
		Points.Add(ArmyGroupPolicy::FitPoint(Polygon, Fit, Offset));
	return Fit;
}
}

FLegPlan PlanLeg(const FFormation& Formation, TConstArrayView<FVector2D> Polygon, const FVector& Centre,
	TConstArrayView<FMarchUnit> Units, ELegShape Shape, float Yaw, int32 Seed)
{
	FLegPlan Plan;
	Plan.Shape = Shape;
	const int32 Count = Units.Num();
	if (!Count)
		return Plan;
	TArray<FVector, TInlineAllocator<8>> Points;
	if (Shape == ELegShape::Column)
	{
		Plan.Fit = ColumnPoints(Formation, Polygon, Centre, Units, Yaw, Seed, Points);
		// A file the fit squeezed would overlap capsules: plan the box instead.
		if (Plan.Fit.bClamped || ColumnSpacing * Plan.Fit.Scale < ColumnFloorSpacing)
		{
			Points.Reset();
			Plan.Shape = ELegShape::Box;
		}
	}
	if (Plan.Shape == ELegShape::Box)
	{
		Plan.Fit = FitForce(Formation, Polygon, Centre);
		for (const FMarchUnit& Unit : Units)
			Points.Add(FittedSlot(Formation, Plan.Fit, Polygon, Unit.Slot));
	}
	TArray<FVector2D, TInlineAllocator<8>> Positions, Slots;
	TArray<int32, TInlineAllocator<8>> Ranks, SlotOfUnit;
	for (const FMarchUnit& Unit : Units)
	{
		Positions.Add(Unit.Position);
		Ranks.Add(Unit.Rank);
	}
	for (const FVector& Point : Points)
		Slots.Add(FVector2D(Point));
	AssignSlots(Positions, Ranks, Slots, FVector2D(FMath::Cos(Yaw), FMath::Sin(Yaw)), SlotOfUnit);
	for (int32 Index = 0; Index < Count; ++Index)
		Plan.Targets.Add(Points[SlotOfUnit[Index]]);
	return Plan;
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
