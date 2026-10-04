#pragma once

#include "CanvasItem.h"
#include "CoreMinimal.h"
#include "Engine/Canvas.h"

namespace CommandMinimap
{
inline bool Finite(FVector2D Point)
{
	return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y);
}

// The arena square on screen: world to pixel projection and primitives clipped to the square.
struct FMap
{
	UCanvas* Canvas;
	FVector2D Origin;
	float Size;
	FVector2D Extent;

	FVector2D Project(const FVector& World) const
	{
		return Origin + FVector2D((World.Y + Extent.Y) / (2.0 * Extent.Y), (Extent.X - World.X) / (2.0 * Extent.X)) * Size;
	}

	bool Point(const FVector& World, FVector2D& Screen) const
	{
		if (!FMath::IsFinite(World.X) || !FMath::IsFinite(World.Y)
			|| FMath::Abs(World.X) > Extent.X || FMath::Abs(World.Y) > Extent.Y)
			return false;
		Screen = Project(World);
		return true;
	}

	void Fill(FVector2D Position, FVector2D Dimensions, const FLinearColor& Color) const
	{
		const FVector2D End = Position + Dimensions;
		Position.X = FMath::Max(Position.X, Origin.X);
		Position.Y = FMath::Max(Position.Y, Origin.Y);
		Dimensions = FVector2D(FMath::Min(End.X, Origin.X + Size), FMath::Min(End.Y, Origin.Y + Size)) - Position;
		if (Dimensions.X <= 0.0 || Dimensions.Y <= 0.0)
			return;
		FCanvasTileItem Item(Position, Dimensions, Color);
		Item.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(Item);
	}

	// Parametric segment clipping also keeps symbols/territory rings inside the square.
	void Line(FVector2D A, FVector2D B, const FLinearColor& Color, float Thickness = 1.f) const
	{
		if (!Finite(A) || !Finite(B))
			return;
		const FVector2D Delta = B - A;
		double Start = 0.0, End = 1.0;
		for (int32 Axis = 0; Axis < 2; ++Axis)
		{
			const double D = Axis == 0 ? Delta.X : Delta.Y;
			const double P = Axis == 0 ? A.X : A.Y;
			const double Min = Axis == 0 ? Origin.X : Origin.Y;
			if (D == 0.0)
			{
				if (P < Min || P > Min + Size)
					return;
				continue;
			}
			const double T0 = (Min - P) / D, T1 = (Min + Size - P) / D;
			Start = FMath::Max(Start, FMath::Min(T0, T1));
			End = FMath::Min(End, FMath::Max(T0, T1));
			if (Start > End)
				return;
		}
		FCanvasLineItem Item(A + Delta * Start, A + Delta * End);
		Item.SetColor(Color);
		Item.LineThickness = Thickness;
		Canvas->DrawItem(Item);
	}

	void Box(FVector2D Center, double Radius, const FLinearColor& Color) const
	{
		const FVector2D A = Center - FVector2D(Radius, Radius), B = Center + FVector2D(Radius, Radius);
		Line(A, FVector2D(B.X, A.Y), Color);
		Line(FVector2D(B.X, A.Y), B, Color);
		Line(B, FVector2D(A.X, B.Y), Color);
		Line(FVector2D(A.X, B.Y), A, Color);
	}

	void Diamond(FVector2D Center, double Radius, const FLinearColor& Color) const
	{
		const FVector2D Top = Center + FVector2D(0, -Radius), Right = Center + FVector2D(Radius, 0);
		const FVector2D Bottom = Center + FVector2D(0, Radius), Left = Center + FVector2D(-Radius, 0);
		Line(Top, Right, Color);
		Line(Right, Bottom, Color);
		Line(Bottom, Left, Color);
		Line(Left, Top, Color);
	}
};
}
