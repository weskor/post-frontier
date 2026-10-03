#include "HoldPolicy.h"
#include "PlacementPolicy.h"

namespace
{
double Cross(const FVector2D& A, const FVector2D& B)
{
	return A.X * B.Y - A.Y * B.X;
}

FVector2D ClosestOnEdge(const FVector2D& A, const FVector2D& B, const FVector2D& Point)
{
	const FVector2D Edge = B - A;
	const double LengthSquared = Edge.SizeSquared();
	return LengthSquared > 0.
		? A + Edge * FMath::Clamp(FVector2D::DotProduct(Point - A, Edge) / LengthSquared, 0., 1.)
		: A;
}

FVector Centroid(TConstArrayView<FVector> Points)
{
	FVector Sum = FVector::ZeroVector;
	for (const FVector& Point : Points)
		Sum += Point;
	return Points.Num() > 0 ? Sum / Points.Num() : Sum;
}
}

bool HoldPolicy::UpdateClock(FClock& Clock, double Now, bool bAlarm, bool bSelected)
{
	if (!Clock.bResponding)
	{
		Clock.QuietSince = -1.;
		if (bAlarm && bSelected)
		{
			Clock.bResponding = true;
			Clock.Started = Now;
		}
		return Clock.bResponding;
	}
	if (bAlarm)
		Clock.QuietSince = -1.;
	else
	{
		if (Clock.QuietSince < 0.)
			Clock.QuietSince = Now;
		if (Now >= FMath::Max(Clock.Started + CommitSeconds, Clock.QuietSince + QuietSeconds))
		{
			Clock.bResponding = false;
			Clock.QuietSince = -1.;
		}
	}
	return Clock.bResponding;
}

void HoldPolicy::SelectResponders(TArrayView<FCandidate> Candidates, int32 ThreatPower)
{
	int64 SelectedPower = 0;
	for (FCandidate& Candidate : Candidates)
	{
		Candidate.bSelected = Candidate.bResponding;
		if (Candidate.bSelected)
			SelectedPower += FMath::Max(Candidate.Power, 0);
	}
	const double RequiredPower = FMath::Max(ThreatPower, 0) * ResponseRatio;
	while (SelectedPower < RequiredPower)
	{
		int32 Nearest = INDEX_NONE;
		for (int32 Index = 0; Index < Candidates.Num(); ++Index)
		{
			const FCandidate& Candidate = Candidates[Index];
			if (!Candidate.bSelected && Candidate.Power > 0
				&& (Nearest == INDEX_NONE || Candidate.DistanceSquared < Candidates[Nearest].DistanceSquared))
				Nearest = Index;
		}
		if (Nearest == INDEX_NONE)
			break;
		Candidates[Nearest].bSelected = true;
		SelectedPower += Candidates[Nearest].Power;
	}
}

bool HoldPolicy::IsAlarmSource(bool bAlive, bool bHostile, bool bInside, bool bDamagingInside)
{
	return bAlive && bHostile && (bInside || bDamagingInside);
}

bool HoldPolicy::IsDamageCurrent(double Now, double Expires, bool bVictimAlive, bool bWithinWeaponRange)
{
	return Now < Expires && bVictimAlive && bWithinWeaponRange;
}

bool HoldPolicy::WithinLeash(bool bInside, bool bDamagingInside, double DistanceToBorder, double WeaponRange)
{
	return bInside || (bDamagingInside && DistanceToBorder <= WeaponRange);
}

bool HoldPolicy::Contains(TConstArrayView<FVector2D> Polygon, const FVector2D& Point)
{
	return PlacementPolicy::ContainsPoint(Polygon, Point);
}

FVector2D HoldPolicy::ClosestBoundary(TConstArrayView<FVector2D> Polygon, const FVector2D& Point)
{
	FVector2D Closest = Point;
	double BestDistance = TNumericLimits<double>::Max();
	for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
	{
		const FVector2D Candidate = ClosestOnEdge(Polygon[Previous], Polygon[Index], Point);
		const double Distance = (Candidate - Point).SizeSquared();
		if (Distance < BestDistance)
		{
			BestDistance = Distance;
			Closest = Candidate;
		}
	}
	return Closest;
}

FVector2D HoldPolicy::ClampInside(TConstArrayView<FVector2D> Polygon, const FVector2D& Point)
{
	if (Contains(Polygon, Point))
		return Point;
	const FVector2D Boundary = ClosestBoundary(Polygon, Point);
	if (Polygon.Num() < 3)
		return Boundary;

	// Use the nearest edge's local interior, not a centroid that may lie outside a concavity.
	double SignedArea = 0.;
	for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
		SignedArea += Cross(Polygon[Previous], Polygon[Index]);
	for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
	{
		const FVector2D& A = Polygon[Previous];
		const FVector2D& B = Polygon[Index];
		const FVector2D Edge = B - A;
		const double Length = Edge.Size();
		if (Length == 0. || (ClosestOnEdge(A, B, Point) - Boundary).SizeSquared() > 1.e-12)
			continue;
		const FVector2D Inward = FVector2D(-Edge.Y, Edge.X) * ((SignedArea >= 0. ? 1. : -1.) / Length);
		const double Step = FMath::Min(.01, Length * .001);
		// At a vertex, first move a little along the edge to avoid crossing its neighbour.
		const FVector2D Along = Boundary + ((A + B) * .5 - Boundary).GetSafeNormal() * Step;
		for (double Inset = Step; Inset > 1.e-7; Inset *= .5)
		{
			const FVector2D Candidate = Along + Inward * Inset;
			if (Contains(Polygon, Candidate)
				&& (ClosestBoundary(Polygon, Candidate) - Candidate).SizeSquared() > 1.e-12)
				return Candidate;
		}
	}
	// A boundary remains a legal clamp even if a degenerate sliver has no usable inset.
	return Boundary;
}

bool HoldPolicy::SegmentInside(TConstArrayView<FVector2D> Polygon, const FVector2D& Start, const FVector2D& End)
{
	if (!Contains(Polygon, Start) || !Contains(Polygon, End))
		return false;
	const FVector2D Direction = End - Start;
	const double LengthSquared = Direction.SizeSquared();
	if (LengthSquared == 0.)
		return true;
	double Current = 0.;
	while (Current < 1.)
	{
		double Next = 1.;
		for (int32 Index = 0, Previous = Polygon.Num() - 1; Index < Polygon.Num(); Previous = Index++)
		{
			const FVector2D& A = Polygon[Previous];
			const FVector2D& B = Polygon[Index];
			const FVector2D Edge = B - A;
			const double Denominator = Cross(Direction, Edge);
			if (Denominator != 0.)
			{
				const double T = Cross(A - Start, Edge) / Denominator;
				const double U = Cross(A - Start, Direction) / Denominator;
				if (T > Current && T < Next && U >= 0. && U <= 1.)
					Next = T;
			}
			else if (Cross(A - Start, Direction) == 0.)
			{
				const double TA = FVector2D::DotProduct(A - Start, Direction) / LengthSquared;
				const double TB = FVector2D::DotProduct(B - Start, Direction) / LengthSquared;
				if (TA > Current && TA < Next)
					Next = TA;
				if (TB > Current && TB < Next)
					Next = TB;
			}
		}
		if (!Contains(Polygon, Start + Direction * ((Current + Next) * .5)))
			return false;
		Current = Next;
	}
	return true;
}

int32 HoldPolicy::ChoosePost(TConstArrayView<FVector> Posts, TConstArrayView<int32> Occupancy,
	TConstArrayView<FVector> Assets, TConstArrayView<FVector> HostileBorders)
{
	if (Posts.Num() == 0)
		return INDEX_NONE;
	// Ranking by sum of squared distances to all asset/border midpoints is exactly
	// ranking by distance to their mean; avoid materialising or visiting every pair.
	const FVector Center = Assets.Num() > 0 && HostileBorders.Num() > 0
		? (Centroid(Assets) + Centroid(HostileBorders)) * .5
		: Centroid(Assets.Num() > 0 ? Assets : HostileBorders.Num() > 0 ? HostileBorders
																		: Posts);
	int32 Best = INDEX_NONE;
	int32 BestOccupancy = 0;
	double BestDistance = 0.;
	for (int32 Index = 0; Index < Posts.Num(); ++Index)
	{
		const int32 Count = Index < Occupancy.Num() ? Occupancy[Index] : 0;
		const double Distance = FVector::DistSquared(Posts[Index], Center);
		if (Best == INDEX_NONE || Count < BestOccupancy || (Count == BestOccupancy && Distance < BestDistance))
		{
			Best = Index;
			BestOccupancy = Count;
			BestDistance = Distance;
		}
	}
	return Best;
}

int32 HoldPolicy::ChoosePostSlot(TConstArrayView<int32> UsedSlots)
{
	int32 Slot = 0;
	for (;; ++Slot)
	{
		bool bUsed = false;
		for (const int32 Used : UsedSlots)
			if (Used == Slot)
			{
				bUsed = true;
				break;
			}
		if (!bUsed)
			return Slot;
	}
}

FVector HoldPolicy::SharedPostOffset(int32 OccupantIndex)
{
	if (OccupantIndex <= 0)
		return FVector::ZeroVector;
	int64 Ring = 1;
	int64 Slot = static_cast<int64>(OccupantIndex) - 1;
	while (Slot >= 6 * Ring)
	{
		Slot -= 6 * Ring;
		++Ring;
	}
	const double Angle = 2. * PI * static_cast<double>(Slot) / (6. * Ring);
	const double Radius = 800. * Ring;
	return FVector(FMath::Cos(Angle) * Radius, FMath::Sin(Angle) * Radius, 0.);
}

int32 HoldPolicy::ChooseThreat(TConstArrayView<FVector> ThreatPositions, TConstArrayView<bool> Permitted,
	const FVector& ForceLocation, int32 CurrentIndex)
{
	if (CurrentIndex >= 0 && CurrentIndex < ThreatPositions.Num() && CurrentIndex < Permitted.Num() && Permitted[CurrentIndex])
		return CurrentIndex;
	int32 Best = INDEX_NONE;
	double BestDistance = 0.;
	for (int32 Index = 0; Index < ThreatPositions.Num() && Index < Permitted.Num(); ++Index)
	{
		if (!Permitted[Index])
			continue;
		const double Distance = FVector::DistSquared(ForceLocation, ThreatPositions[Index]);
		if (Best == INDEX_NONE || Distance < BestDistance)
		{
			Best = Index;
			BestDistance = Distance;
		}
	}
	return Best;
}
