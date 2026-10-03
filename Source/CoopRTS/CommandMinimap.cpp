#include "CommandMinimap.h"

#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CanvasItem.h"
#include "CapturePoint.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "MapRegion.h"
#include "DepositSite.h"
#include "CommandPlayerController.h"
#include "Commands/PingCommandComponent.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "Headquarters.h"
#include "JevIntentView.h"

namespace
{
const FLinearColor Friendly(.25f, .72f, .64f);
const FLinearColor Hostile(.90f, .36f, .32f);
const FLinearColor Neutral(.46f, .50f, .55f);
const FLinearColor Contested(.94f, .72f, .34f);
const FLinearColor View(.75f, .83f, .88f, .80f);

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
	return Team == 0 ? Friendly : Team == 5 ? Hostile
											: Neutral;
}

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

void DrawFootprint(const FMap& Map, ACommandPlayerController* Controller)
{
	int32 Width = 0, Height = 0;
	Controller->GetViewportSize(Width, Height);
	if (Width <= 0 || Height <= 0)
		return;
	// A clipped convex quadrilateral has at most eight vertices. Stack storage only.
	FVector2D Buffers[2][8];
	for (int32 Corner = 0; Corner < 4; ++Corner)
	{
		const float X = Corner == 1 || Corner == 2 ? Width : 0;
		const float Y = Corner >= 2 ? Height : 0;
		FVector RayOrigin, RayDirection;
		if (!Controller->DeprojectScreenPositionToWorld(X, Y, RayOrigin, RayDirection)
			|| RayOrigin.ContainsNaN() || RayDirection.ContainsNaN() || FMath::Abs(RayDirection.Z) < 1.e-6)
			return;
		const double Distance = -RayOrigin.Z / RayDirection.Z;
		if (!FMath::IsFinite(Distance) || Distance < 0.0)
			return;
		Buffers[0][Corner] = Map.Project(RayOrigin + RayDirection * Distance);
		if (!Finite(Buffers[0][Corner]))
			return;
	}
	int32 Count = 4, Current = 0;
	// Polygon clipping, not independent corner clamping: preserves real intersections
	// and draws the map boundary even when the whole arena lies inside the view.
	for (int32 Edge = 0; Edge < 4 && Count > 0; ++Edge)
	{
		const bool bX = Edge < 2, bMin = Edge % 2 == 0;
		const double Boundary = (bX ? Map.Origin.X : Map.Origin.Y) + (bMin ? 0.0 : Map.Size);
		auto SignedDistance = [=](FVector2D Point) {
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
				Buffers[1 - Current][NextCount++] = Previous + (Point - Previous) * (PreviousDistance / (PreviousDistance - Distance));
			if (Distance >= 0.0)
				Buffers[1 - Current][NextCount++] = Point;
			Previous = Point;
			PreviousDistance = Distance;
		}
		Count = NextCount;
		Current = 1 - Current;
	}
	for (int32 Index = 0; Index < Count; ++Index)
		Map.Line(Buffers[Current][Index], Buffers[Current][(Index + 1) % Count], View);
}

// A region a JEV plan targets: red box with its countdown, amber with ESC while the plan defends it.
void DrawJevBadges(const FMap& Map, const ACommandGameState& State)
{
	JevIntentView::FPlans Plans;
	JevIntentView::Snapshot(State, Plans);
	JevIntent::FBadges Badges;
	JevIntent::BuildBadges(Plans, JevIntentView::Now(State), Badges);
	if (Badges.IsEmpty() || !GEngine || !GEngine->GetSmallFont())
		return;
	const float TextScale = FMath::Clamp(Map.Size / 210.f, .65f, 1.f);
	const FSlateFontInfo Font(GEngine->GetSmallFont(), 9.f * TextScale, FName(TEXT("Bold")));
	for (const JevIntent::FRegionBadge& Badge : Badges)
	{
		FVector2D Point;
		if (!Map.Point(State.GetRegionAnchor(Badge.Region), Point))
			continue;
		const FLinearColor Color = Badge.bEscalated ? Contested : Hostile;
		const FLinearColor TextColor = Badge.bEscalated ? FLinearColor(1.f, .86f, .5f) : FLinearColor(1.f, .62f, .56f);
		TStringBuilder<16> Label;
		if (Badge.bEscalated)
			Label << TEXT("ESC");
		else
			JevIntent::AppendCountdown(Label, Badge.Seconds);
		Map.Box(Point, 5.5, Color);
		const double Width = Label.Len() * 5.2 * TextScale;
		const double Height = 10.0 * TextScale;
		double Left = Point.X + 8.0;
		if (Left + Width > Map.Origin.X + Map.Size)
			Left = Point.X - 8.0 - Width;
		const double Top = FMath::Clamp<double>(Point.Y - Height * .5, Map.Origin.Y, Map.Origin.Y + Map.Size - Height);
		Map.Fill(FVector2D(Left - 1.0, Top), FVector2D(Width + 2.0, Height), FLinearColor(.01f, .015f, .02f, .82f));
		FCanvasTextStringViewItem Text(FVector2D(Left, Top), Label.ToView(), Font, TextColor);
		Map.Canvas->DrawItem(Text);
	}
}
}

bool CommandMinimap::ScreenToWorld(const AArenaBounds* Arena, FVector2D Position, FVector2D Origin, float Size, FVector& OutWorld)
{
	if (!IsValid(Arena) || !ValidSquare(Origin, Size) || !Finite(Position))
		return false;
	const FVector2D UV = (Position - Origin) / Size;
	if (!Finite(UV) || UV.X < 0.0 || UV.X > 1.0 || UV.Y < 0.0 || UV.Y > 1.0)
		return false;
	const FVector2D Extent = Arena->HalfExtent;
	OutWorld = FVector(Extent.X - UV.Y * (2.0 * Extent.X), UV.X * (2.0 * Extent.Y) - Extent.Y, 0.0);
	return true;
}

void CommandMinimap::Draw(UCanvas* Canvas, ACommandPlayerController* Controller, FVector2D Origin, float Size)
{
	if (!Canvas || !IsValid(Controller) || !ValidSquare(Origin, Size))
		return;
	UWorld* World = Controller->GetWorld();
	const AArenaBounds* Arena = AArenaBounds::Find(World);
	if (!Arena)
		return;
	const FMap Map{ Canvas, Origin, Size, Arena->HalfExtent };
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
		for (const AMapRegion* Region : State->Regions)
		{
			if (!IsValid(Region))
				continue;
			const int32 Team = State->GetRegionController(Region->RegionIndex);
			const FLinearColor Color = TeamColor(Team);
			for (int32 Index = 0; Index < Region->Polygon.Num(); ++Index)
			{
				const FVector2D& A = Region->Polygon[Index];
				const FVector2D& B = Region->Polygon[(Index + 1) % Region->Polygon.Num()];
				FVector2D Start, End;
				if (Map.Point(FVector(A.X, A.Y, 0.f), Start) && Map.Point(FVector(B.X, B.Y, 0.f), End))
					Map.Line(Start, End, Color.CopyWithNewOpacity(.45f));
			}
		}
		for (const ADepositSite* Deposit : State->Deposits)
		{
			FVector2D Point;
			if (IsValid(Deposit) && Map.Point(Deposit->GetActorLocation(), Point))
				Map.Diamond(Point, 2.0, Deposit->Remaining > 0 ? Contested : Neutral);
		}
		for (const ACapturePoint* Site : State->CaptureSites)
		{
			FVector2D Point;
			if (!IsValid(Site) || !Map.Point(Site->GetActorLocation(), Point))
				continue;
			const FLinearColor Color = TeamColor(Site->ControllingTeam);
			Map.Diamond(Point, 4.0, Color);
			if (Site->bFriendlyPresent && Site->bEnemyPresent)
				Map.Diamond(Point, 6.5, Contested);
		}
		for (const ACommandBuilding* Building : State->Buildings)
		{
			FVector2D Point;
			if (!IsValid(Building) || !Building->IsAlive() || !Map.Point(Building->GetActorLocation(), Point))
				continue;
			const FLinearColor Color = TeamColor(Building->TeamIndex);
			Map.Box(Point, 3.0, Color);
			if (Building->IsComplete())
				Map.Fill(Point - FVector2D(1, 1), FVector2D(2, 2), Color);
			if (Building == Controller->GetSelectedBuilding())
				Map.Box(Point, 5.0, View);
		}
		for (const AHeadquarters* HQ : { State->FriendlyHeadquarters.Get(), State->EnemyHeadquarters.Get() })
		{
			FVector2D Point;
			if (!IsValid(HQ) || !Map.Point(HQ->GetActorLocation(), Point))
				continue;
			const FLinearColor Color = HQ->IsAlive() ? TeamColor(HQ->TeamIndex) : Neutral;
			Map.Box(Point, 6.0, Color);
			Map.Fill(Point - FVector2D(3, 3), FVector2D(6, 6), Color);
		}
	}
	// Read existing level actor arrays directly; no per-frame actor snapshots/allocations.
	for (const ULevel* Level : World->GetLevels())
	{
		if (!Level)
			continue;
		for (const AActor* Actor : Level->Actors)
		{
			if (!IsValid(Actor))
				continue;
			FVector2D Point;
			if (const AArmyUnit* Unit = Cast<AArmyUnit>(Actor))
			{
				if (Unit->IsAlive() && Map.Point(Unit->GetActorLocation(), Point))
					Map.Fill(Point - FVector2D(1, 1), FVector2D(2, 2), TeamColor(Unit->GetTeamIndex()));
			}
			else if (const AArmyGroup* Squad = Cast<AArmyGroup>(Actor))
			{
				bool bAlive = false;
				for (const AArmyUnit* Member : Squad->GetUnits())
					if (IsValid(Member) && Member->IsAlive())
					{
						bAlive = true;
						break;
					}
				if (bAlive && Map.Point(Squad->GetCenter(), Point))
					Map.Diamond(Point, 3.0, TeamColor(Squad->GetTeamIndex()));
			}
		}
	}
	if (const ACommandGameState* State = World->GetGameState<ACommandGameState>())
		DrawJevBadges(Map, *State);
	const ACommandBuilding* Selected = Controller->GetSelectedBuilding();
	FVector2D Front;
	if (IsValid(Selected) && Selected->IsAlive() && IsValid(Selected->ForceGroup)
		&& Map.Point(Selected->ForceGroup->Destination, Front))
	{
		Map.Diamond(Front, 6.0, Contested);
		Map.Line(Front - FVector2D(3, 0), Front + FVector2D(3, 0), Contested);
		Map.Line(Front - FVector2D(0, 3), Front + FVector2D(0, 3), Contested);
	}
	if (const ACommandGameState* State = World->GetGameState<ACommandGameState>(); State && Controller->PingCommands)
	{
		const float Now = State->GetServerWorldTimeSeconds();
		for (const FObjectiveEvent& Event : Controller->PingCommands->GetEvents())
		{
			FVector2D Point;
			if (Now - Event.ServerTime >= UPingCommandComponent::Lifetime || Event.Forces.IsEmpty()
				|| !Map.Point(Event.Location, Point))
				continue;
			const FLinearColor Color = AArmyUnit::GetCommanderColor(Event.Forces[0].CommanderIndex);
			Map.Diamond(Point, 7.0, Color);
			Map.Line(Point - FVector2D(4, 0), Point + FVector2D(4, 0), Color, 2.f);
			Map.Line(Point - FVector2D(0, 4), Point + FVector2D(0, 4), Color, 2.f);
		}
	}
	Map.Box(Origin + FVector2D(Size, Size) * .5, Size * .5, Grid);
	DrawFootprint(Map, Controller);
	if (GEngine && GEngine->GetSmallFont())
	{
		const float TextScale = FMath::Clamp(Size / 210.f, .65f, 1.f);
		const FSlateFontInfo Font(GEngine->GetSmallFont(), 9.f * TextScale, FName(TEXT("Regular")));
		FCanvasTextStringViewItem Header(Origin - FVector2D(0, 29.f * TextScale),
			FStringView(Controller->IsAssigningOrder() ? TEXT("ARENA / LMB ATTACK / RMB CANCEL") : TEXT("ARENA / LMB PAN / RMB ORDER")), Font, View);
		Canvas->DrawItem(Header);
		FCanvasTextStringViewItem Legend(Origin - FVector2D(0, 15.f * TextScale),
			FStringView(TEXT("HQ/base | sector | amber: contest/front")), Font, Neutral);
		Canvas->DrawItem(Legend);
	}
}
