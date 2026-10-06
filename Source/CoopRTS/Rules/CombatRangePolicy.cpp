#include "Rules/CombatRangePolicy.h"

namespace CombatRangePolicy
{
bool IsStructure(const FRangeTarget& Target)
{
	return Target.HalfExtent.X > 0. || Target.HalfExtent.Y > 0.;
}

namespace
{
// Into the box's own axes, centred on the box.
FVector2D ToLocal(const FVector2D& World, const FRangeTarget& Target)
{
	double Sin = 0., Cos = 1.;
	FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(static_cast<double>(Target.YawDegrees)));
	const FVector2D Offset = World - Target.Center;
	return FVector2D(Cos * Offset.X + Sin * Offset.Y, -Sin * Offset.X + Cos * Offset.Y);
}

FVector2D ToWorld(const FVector2D& Local, const FRangeTarget& Target)
{
	double Sin = 0., Cos = 1.;
	FMath::SinCos(&Sin, &Cos, FMath::DegreesToRadians(static_cast<double>(Target.YawDegrees)));
	return Target.Center + FVector2D(Cos * Local.X - Sin * Local.Y, Sin * Local.X + Cos * Local.Y);
}
}

FRangeTarget Box(const FVector& Center, const FVector2D& HalfExtent, float YawDegrees, float AttackerRadius, bool bBlocksMovement)
{
	FRangeTarget Target(Center);
	Target.HalfExtent = HalfExtent;
	Target.YawDegrees = YawDegrees;
	Target.Clearance = AttackerRadius;
	Target.bBlocksMovement = bBlocksMovement;
	return Target;
}

FVector2D NearestPoint(const FVector2D& From, const FRangeTarget& Target)
{
	if (!IsStructure(Target))
		return Target.Center;
	const FVector2D Local = ToLocal(From, Target);
	return ToWorld(FVector2D(FMath::Clamp(Local.X, -Target.HalfExtent.X, Target.HalfExtent.X),
					   FMath::Clamp(Local.Y, -Target.HalfExtent.Y, Target.HalfExtent.Y)),
		Target);
}

double EdgeDistance(const FVector2D& From, const FRangeTarget& Target)
{
	return FMath::Max(0., FVector2D::Distance(From, NearestPoint(From, Target)) - Target.Clearance);
}

bool InRange(const FVector2D& From, const FRangeTarget& Target, double Range)
{
	return EdgeDistance(From, Target) <= Range;
}

FVector2D PointAtEdgeDistance(const FVector2D& From, const FRangeTarget& Target, double Standoff)
{
	if (IsStructure(Target))
	{
		// Inside the box the way out is the nearest face, along its normal.
		const FVector2D Local = ToLocal(From, Target);
		if (FMath::Abs(Local.X) <= Target.HalfExtent.X && FMath::Abs(Local.Y) <= Target.HalfExtent.Y)
		{
			const bool bThroughX = Target.HalfExtent.X - FMath::Abs(Local.X) < Target.HalfExtent.Y - FMath::Abs(Local.Y);
			const double Side = (bThroughX ? Local.X : Local.Y) < 0. ? -1. : 1.;
			const FVector2D Normal = bThroughX ? FVector2D(Side, 0.) : FVector2D(0., Side);
			const FVector2D OnFace = bThroughX ? FVector2D(Side * Target.HalfExtent.X, Local.Y) : FVector2D(Local.X, Side * Target.HalfExtent.Y);
			return ToWorld(OnFace + Normal * (Standoff + Target.Clearance), Target);
		}
	}
	const FVector2D Nearest = NearestPoint(From, Target);
	FVector2D Direction = (From - Nearest).GetSafeNormal();
	if (Direction.IsNearlyZero())
		Direction = (From - Target.Center).GetSafeNormal();
	if (Direction.IsNearlyZero())
		Direction = FVector2D(-1., 0.);
	return Nearest + Direction * (Standoff + Target.Clearance);
}
}
