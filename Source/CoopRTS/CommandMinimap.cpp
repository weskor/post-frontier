#include "CommandMinimap.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CanvasItem.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Headquarters.h"

namespace
{
	constexpr double ArenaExtent = 4500.0;
	const FLinearColor Friendly(.25f, .72f, .64f);
	const FLinearColor Enemy(.90f, .36f, .32f);
	const FLinearColor Neutral(.46f, .50f, .55f);
	const FLinearColor Contested(.94f, .72f, .34f);
	const FLinearColor View(.75f, .83f, .88f, .80f);
	const FVector2D Ring[] = {
		{1, 0}, {.923880, .382683}, {.707107, .707107}, {.382683, .923880},
		{0, 1}, {-.382683, .923880}, {-.707107, .707107}, {-.923880, .382683},
		{-1, 0}, {-.923880, -.382683}, {-.707107, -.707107}, {-.382683, -.923880},
		{0, -1}, {.382683, -.923880}, {.707107, -.707107}, {.923880, -.382683}
	};

	bool Finite(FVector2D Point)
	{
		return FMath::IsFinite(Point.X) && FMath::IsFinite(Point.Y);
	}

	bool ValidSquare(FVector2D Origin, float Size)
	{
		return Finite(Origin) && FMath::IsFinite(Size) && Size > 0.f
			&& Finite(Origin + FVector2D(Size, Size));
	}

	FLinearColor TeamColor(int32 Team)
	{
		return Team == 0 ? Friendly : Team == 5 ? Enemy : Neutral;
	}

	struct FMap
	{
		UCanvas* Canvas;
		FVector2D Origin;
		float Size;

		FVector2D Project(const FVector& World) const
		{
			return Origin + FVector2D((World.Y + ArenaExtent) / (2.0 * ArenaExtent),
				(ArenaExtent - World.X) / (2.0 * ArenaExtent)) * Size;
		}

		bool Point(const FVector& World, FVector2D& Screen) const
		{
			if (!FMath::IsFinite(World.X) || !FMath::IsFinite(World.Y)
				|| FMath::Abs(World.X) > ArenaExtent || FMath::Abs(World.Y) > ArenaExtent) return false;
			Screen = Project(World);
			return true;
		}

		void Fill(FVector2D Position, FVector2D Dimensions, const FLinearColor& Color) const
		{
			const FVector2D End = Position + Dimensions;
			Position.X = FMath::Max(Position.X, Origin.X);
			Position.Y = FMath::Max(Position.Y, Origin.Y);
			Dimensions = FVector2D(FMath::Min(End.X, Origin.X + Size), FMath::Min(End.Y, Origin.Y + Size)) - Position;
			if (Dimensions.X <= 0.0 || Dimensions.Y <= 0.0) return;
			FCanvasTileItem Item(Position, Dimensions, Color);
			Item.BlendMode = SE_BLEND_Translucent;
			Canvas->DrawItem(Item);
		}

		// Parametric segment clipping also keeps symbols/territory rings inside the square.
		void Line(FVector2D A, FVector2D B, const FLinearColor& Color, float Thickness = 1.f) const
		{
			if (!Finite(A) || !Finite(B)) return;
			const FVector2D Delta = B - A;
			double Start = 0.0, End = 1.0;
			for (int32 Axis = 0; Axis < 2; ++Axis)
			{
				const double D = Axis == 0 ? Delta.X : Delta.Y;
				const double P = Axis == 0 ? A.X : A.Y;
				const double Min = Axis == 0 ? Origin.X : Origin.Y;
				if (D == 0.0)
				{
					if (P < Min || P > Min + Size) return;
					continue;
				}
				const double T0 = (Min - P) / D, T1 = (Min + Size - P) / D;
				Start = FMath::Max(Start, FMath::Min(T0, T1));
				End = FMath::Min(End, FMath::Max(T0, T1));
				if (Start > End) return;
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
			Line(Top, Right, Color); Line(Right, Bottom, Color);
			Line(Bottom, Left, Color); Line(Left, Top, Color);
		}
	};

	void DrawFootprint(const FMap& Map, ACommandPlayerController* Controller)
	{
		int32 Width = 0, Height = 0;
		Controller->GetViewportSize(Width, Height);
		if (Width <= 0 || Height <= 0) return;
		// A clipped convex quadrilateral has at most eight vertices. Stack storage only.
		FVector2D Buffers[2][8];
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const float X = Corner == 1 || Corner == 2 ? Width : 0;
			const float Y = Corner >= 2 ? Height : 0;
			FVector RayOrigin, RayDirection;
			if (!Controller->DeprojectScreenPositionToWorld(X, Y, RayOrigin, RayDirection)
				|| RayOrigin.ContainsNaN() || RayDirection.ContainsNaN() || FMath::Abs(RayDirection.Z) < 1.e-6) return;
			const double Distance = -RayOrigin.Z / RayDirection.Z;
			if (!FMath::IsFinite(Distance) || Distance < 0.0) return;
			Buffers[0][Corner] = Map.Project(RayOrigin + RayDirection * Distance);
			if (!Finite(Buffers[0][Corner])) return;
		}
		int32 Count = 4, Current = 0;
		// Polygon clipping, not independent corner clamping: preserves real intersections
		// and draws the map boundary even when the whole arena lies inside the view.
		for (int32 Edge = 0; Edge < 4 && Count > 0; ++Edge)
		{
			const bool bX = Edge < 2, bMin = Edge % 2 == 0;
			const double Boundary = (bX ? Map.Origin.X : Map.Origin.Y) + (bMin ? 0.0 : Map.Size);
			auto SignedDistance = [=](FVector2D Point)
			{
				return ((bX ? Point.X : Point.Y) - Boundary) * (bMin ? 1.0 : -1.0);
			};
			int32 NextCount = 0;
			FVector2D Previous = Buffers[Current][Count - 1];
			double PreviousDistance = SignedDistance(Previous);
			for (int32 Index = 0; Index < Count; ++Index)
			{
				const FVector2D Point = Buffers[Current][Index];
				const double Distance = SignedDistance(Point);
				if ((Distance >= 0.0) != (PreviousDistance >= 0.0))
					Buffers[1 - Current][NextCount++] = Previous + (Point - Previous)
						* (PreviousDistance / (PreviousDistance - Distance));
				if (Distance >= 0.0) Buffers[1 - Current][NextCount++] = Point;
				Previous = Point;
				PreviousDistance = Distance;
			}
			Count = NextCount;
			Current = 1 - Current;
		}
		for (int32 Index = 0; Index < Count; ++Index)
			Map.Line(Buffers[Current][Index], Buffers[Current][(Index + 1) % Count], View);
	}
}

bool CommandMinimap::ScreenToWorld(FVector2D Position, FVector2D Origin, float Size, FVector& OutWorld)
{
	if (!ValidSquare(Origin, Size) || !Finite(Position)) return false;
	const FVector2D UV = (Position - Origin) / Size;
	if (!Finite(UV) || UV.X < 0.0 || UV.X > 1.0 || UV.Y < 0.0 || UV.Y > 1.0) return false;
	OutWorld = FVector(ArenaExtent - UV.Y * (2.0 * ArenaExtent), UV.X * (2.0 * ArenaExtent) - ArenaExtent, 0.0);
	return true;
}

void CommandMinimap::Draw(UCanvas* Canvas, ACommandPlayerController* Controller, FVector2D Origin, float Size)
{
	if (!Canvas || !IsValid(Controller) || !ValidSquare(Origin, Size)) return;
	UWorld* World = Controller->GetWorld();
	if (!World) return;
	const FMap Map{Canvas, Origin, Size};
	Map.Fill(Origin, FVector2D(Size, Size), FLinearColor(.018f, .028f, .038f, .96f));
	const FLinearColor Grid(.12f, .17f, .20f, .65f);
	for (int32 Index = 1; Index < 4; ++Index)
	{
		const double Offset = Size * Index / 4.0;
		Map.Line(Origin + FVector2D(Offset, 0), Origin + FVector2D(Offset, Size), Grid);
		Map.Line(Origin + FVector2D(0, Offset), Origin + FVector2D(Size, Offset), Grid);
	}
	if (const ACommandGameState* State = World->GetGameState<ACommandGameState>())
	{
		for (const ACapturePoint* Site : State->CaptureSites)
		{
			FVector2D Point;
			if (!IsValid(Site) || !Map.Point(Site->GetActorLocation(), Point)) continue;
			const FLinearColor Color = TeamColor(Site->ControllingTeam);
			const double Radius = ACapturePoint::TerritoryRadius * Size / (2.0 * ArenaExtent);
			FVector2D Previous = Point + FVector2D(Radius, 0);
			for (int32 Segment = 1; Segment <= 16; ++Segment)
			{
				const FVector2D Next = Point + Ring[Segment % 16] * Radius;
				Map.Line(Previous, Next, FLinearColor(Color.R, Color.G, Color.B, .30f));
				Previous = Next;
			}
			Map.Diamond(Point, 4.0, Color);
			if (Site->bFriendlyPresent && Site->bEnemyPresent) Map.Diamond(Point, 6.5, Contested);
		}
		for (const ACommandBuilding* Building : State->Buildings)
		{
			FVector2D Point;
			if (!IsValid(Building) || !Building->IsAlive() || !Map.Point(Building->GetActorLocation(), Point)) continue;
			const FLinearColor Color = TeamColor(Building->TeamIndex);
			Map.Box(Point, 3.0, Color);
			if (Building->IsComplete()) Map.Fill(Point - FVector2D(1, 1), FVector2D(2, 2), Color);
			if (Building == Controller->GetSelectedBuilding()) Map.Box(Point, 5.0, View);
		}
		for (const AHeadquarters* HQ : {State->FriendlyHeadquarters.Get(), State->EnemyHeadquarters.Get()})
		{
			FVector2D Point;
			if (!IsValid(HQ) || !Map.Point(HQ->GetActorLocation(), Point)) continue;
			const FLinearColor Color = HQ->IsAlive() ? TeamColor(HQ->TeamIndex) : Neutral;
			Map.Box(Point, 6.0, Color);
			Map.Fill(Point - FVector2D(3, 3), FVector2D(6, 6), Color);
		}
	}
	// Read existing level actor arrays directly; no per-frame actor snapshots/allocations.
	for (const ULevel* Level : World->GetLevels())
	{
		if (!Level) continue;
		for (const AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor)) continue;
			FVector2D Point;
			if (const AArmyUnit* Unit = Cast<AArmyUnit>(Actor))
			{
				if (Unit->IsAlive() && Map.Point(Unit->GetActorLocation(), Point))
					Map.Fill(Point - FVector2D(1, 1), FVector2D(2, 2), TeamColor(Unit->TeamIndex));
			}
			else if (const AArmyGroup* Squad = Cast<AArmyGroup>(Actor))
			{
				bool bAlive = false;
				for (const AArmyUnit* Member : Squad->Units)
					if (IsValid(Member) && Member->IsAlive()) { bAlive = true; break; }
				if (bAlive && Map.Point(Squad->GetCenter(), Point))
					Map.Diamond(Point, 3.0, TeamColor(Squad->TeamIndex));
			}
		}
	}
	const ACommandBuilding* Selected = Controller->GetSelectedBuilding();
	FVector2D Front;
	if (IsValid(Selected) && Selected->IsAlive() && Selected->Kind == EBuildingKind::Barracks
		&& Selected->HasConfiguredFront() && Map.Point(Selected->FrontLocation, Front))
	{
		Map.Diamond(Front, 6.0, Contested);
		Map.Line(Front - FVector2D(3, 0), Front + FVector2D(3, 0), Contested);
		Map.Line(Front - FVector2D(0, 3), Front + FVector2D(0, 3), Contested);
	}
	Map.Box(Origin + FVector2D(Size, Size) * .5, Size * .5, Grid);
	DrawFootprint(Map, Controller);
	if (GEngine && GEngine->GetSmallFont())
	{
		const float TextScale = FMath::Clamp(Size / 210.f, .65f, 1.f);
		const FSlateFontInfo Font(GEngine->GetSmallFont(), 9.f * TextScale, FName(TEXT("Regular")));
		FCanvasTextStringViewItem Header(Origin - FVector2D(0, 29.f * TextScale),
			FStringView(TEXT("ARENA  /  CLICK TO PAN")), Font, View);
		Canvas->DrawItem(Header);
		FCanvasTextStringViewItem Legend(Origin - FVector2D(0, 15.f * TextScale),
			FStringView(TEXT("HQ/base | sector | amber: contest/front")), Font, Neutral);
		Canvas->DrawItem(Legend);
	}
}
