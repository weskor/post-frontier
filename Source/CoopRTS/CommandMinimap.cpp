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
#include "Commands/AbilityCommandComponent.h"
#include "CommandPlayerState.h"
#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "GroundHeight.h"
#include "Headquarters.h"
#include "JevIntentView.h"
#include "HUD/MapPresentation.h"
#include "HUD/MinimapMap.h"
#include "HUD/ForceRoutePresentation.h"

namespace
{
const FLinearColor Friendly(.25f, .72f, .64f);
const FLinearColor Hostile(.90f, .36f, .32f);
const FLinearColor Neutral(.46f, .50f, .55f);
const FLinearColor Contested(.94f, .72f, .34f);
const FLinearColor View(.75f, .83f, .88f, .80f);

using CommandMinimap::Finite;
using CommandMinimap::FMap;

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

void DrawFortifyRing(const FMap& Map, const AMapRegion& Region, FVector2D Point)
{
	const float Left = FortifyPolicy::SecondsLeft(Region.GetFortify(), Region.GetServerNow());
	const float Sweep = FMath::Clamp(Left / FortifyPolicy::DurationSeconds, 0.f, 1.f) * 2.f * PI;
	// The gap advances clockwise from twelve o'clock as the remaining arc drains.
	for (float Angle = 2.f * PI - Sweep; Angle < 2.f * PI; Angle += 2.f * PI / 24.f)
	{
		const float End = FMath::Min(Angle + 2.f * PI / 36.f, 2.f * PI);
		const FVector2D A(FMath::Sin(Angle), -FMath::Cos(Angle));
		const FVector2D B(FMath::Sin(End), -FMath::Cos(End));
		Map.Line(Point + A * 9.f, Point + B * 9.f, FLinearColor(.42f, .90f, 1.f), 1.5f);
	}
}

void DrawFootprint(const FMap& Map, ACommandPlayerController* Controller)
{
	int32 Width = 0, Height = 0;
	Controller->GetViewportSize(Width, Height);
	if (Width <= 0 || Height <= 0)
		return;
	// The view's ground plane is the one the camera stands on: z = 0 on flat ground, 300 on a plateau.
	const double PlaneZ = Controller->GetPawn() ? Controller->GetPawn()->GetActorLocation().Z : 0.0;
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
		const double Distance = (PlaneZ - RayOrigin.Z) / RayDirection.Z;
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

const FLinearColor GridColor(.12f, .17f, .20f, .65f);

void DrawBackdrop(const FMap& Map, FVector2D Origin, float Size)
{
	Map.Fill(Origin, FVector2D(Size, Size), FLinearColor(.018f, .028f, .038f, .96f));
	for (int32 Index = 1; Index < 4; ++Index)
	{
		const double Offset = Size * Index / 4.0;
		Map.Line(Origin + FVector2D(Offset, 0), Origin + FVector2D(Offset, Size), GridColor);
		Map.Line(Origin + FVector2D(0, Offset), Origin + FVector2D(Size, Offset), GridColor);
	}
}

void DrawRegionOutlines(const FMap& Map, const ACommandGameState& State, const ACommandPlayerController& Controller)
{
	const ACommandPlayerState* Own = Controller.GetPlayerState<ACommandPlayerState>();
	const bool bTargeting = Controller.IsFortifyTargeting();
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region))
			continue;
		// While targeting, valid regions get a dashed green border and the rest dim.
		const bool bValid = bTargeting
			&& FortifyPolicy::IsValidTarget(FortifyPolicy::Evaluate(UAbilityCommandComponent::MakeFortifyInput(State, Own, Region)).Verdict);
		const FLinearColor Color = bTargeting ? (bValid ? FLinearColor(.46f, .94f, .56f) : Neutral.CopyWithNewOpacity(.2f))
											  : TeamColor(State.GetRegionController(Region->RegionIndex)).CopyWithNewOpacity(.45f);
		for (int32 Index = 0; Index < Region->Polygon.Num(); ++Index)
		{
			const FVector2D& A = Region->Polygon[Index];
			const FVector2D& B = Region->Polygon[(Index + 1) % Region->Polygon.Num()];
			FVector2D Start, End;
			if (!Map.Point(FVector(A.X, A.Y, 0.f), Start) || !Map.Point(FVector(B.X, B.Y, 0.f), End))
				continue;
			if (!bValid)
			{
				Map.Line(Start, End, Color);
				continue;
			}
			const double Length = FVector2D::Distance(Start, End);
			const FVector2D Direction = (End - Start).GetSafeNormal();
			for (double Offset = 0.; Offset < Length; Offset += 6.)
				Map.Line(Start + Direction * Offset, Start + Direction * FMath::Min(Offset + 3., Length), Color);
		}
	}
	MapView::DrawMinimapMarks(State, Map);
}

void DrawFortifyRings(const FMap& Map, const ACommandGameState& State)
{
	for (const AMapRegion* Region : State.Regions)
	{
		FVector2D Point;
		if (IsValid(Region) && Region->IsFortifyActive() && Map.Point(State.GetRegionAnchor(Region->RegionIndex), Point))
			DrawFortifyRing(Map, *Region, Point);
	}
}

void DrawSites(const FMap& Map, const ACommandGameState& State, const ACommandPlayerController& Controller)
{
	for (const ADepositSite* Deposit : State.Deposits)
	{
		FVector2D Point;
		if (IsValid(Deposit) && Map.Point(Deposit->GetActorLocation(), Point))
			Map.Diamond(Point, 2.0, Deposit->Remaining > 0 ? Contested : Neutral);
	}
	for (const ACapturePoint* Site : State.CaptureSites)
	{
		FVector2D Point;
		if (!IsValid(Site) || !Map.Point(Site->GetActorLocation(), Point))
			continue;
		FLinearColor Color = TeamColor(Site->ControllingTeam);
		const ACommandPlayerState* Own = Controller.GetPlayerState<ACommandPlayerState>();
		if (Controller.IsFortifyTargeting() && (!Own || Site->ControllingTeam != Own->TeamIndex))
			Color = Neutral.CopyWithNewOpacity(.25f);
		Map.Diamond(Point, 4.0, Color);
		if (Site->bFriendlyPresent && Site->bEnemyPresent)
			Map.Diamond(Point, 6.5, Contested);
	}
}

void DrawStructures(const FMap& Map, const ACommandGameState& State, const ACommandPlayerController& Controller)
{
	for (const ACommandBuilding* Building : State.Buildings)
	{
		FVector2D Point;
		if (!IsValid(Building) || !Building->IsAlive() || !Map.Point(Building->GetActorLocation(), Point))
			continue;
		const FLinearColor Color = TeamColor(Building->TeamIndex);
		Map.Box(Point, 3.0, Color);
		if (Building->IsComplete())
			Map.Fill(Point - FVector2D(1, 1), FVector2D(2, 2), Color);
		if (Building == Controller.GetSelectedBuilding())
			Map.Box(Point, 5.0, View);
	}
	for (const AHeadquarters* HQ : { State.FriendlyHeadquarters.Get(), State.EnemyHeadquarters.Get() })
	{
		FVector2D Point;
		if (!IsValid(HQ) || !Map.Point(HQ->GetActorLocation(), Point))
			continue;
		if (HQ->IsOffline())
		{
			// Offline (0 HP, the attackers hold its main): the team's outline over a dim fill and an amber cross,
			// so it is neither a standing HQ nor a lost one.
			const FLinearColor Team = TeamColor(HQ->TeamIndex);
			Map.Fill(Point - FVector2D(6, 6), FVector2D(12, 12), Team.CopyWithNewOpacity(.22f));
			Map.Box(Point, 6.0, Team);
			Map.Line(Point - FVector2D(5, 5), Point + FVector2D(5, 5), Contested, 1.5f);
			Map.Line(Point + FVector2D(-5, 5), Point + FVector2D(5, -5), Contested, 1.5f);
			continue;
		}
		const FLinearColor Color = HQ->IsAlive() ? TeamColor(HQ->TeamIndex) : Neutral;
		Map.Box(Point, 6.0, Color);
		Map.Fill(Point - FVector2D(3, 3), FVector2D(6, 6), Color);
	}
}

bool HasLivingMember(const AArmyGroup& Squad)
{
	for (const AArmyUnit* Member : Squad.GetUnits())
		if (IsValid(Member) && Member->IsAlive())
			return true;
	return false;
}

// Read existing level actor arrays directly; no per-frame actor snapshots/allocations.
void DrawForces(const FMap& Map, const UWorld& World)
{
	for (const ULevel* Level : World.GetLevels())
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
				if (HasLivingMember(*Squad) && Map.Point(Squad->GetCenter(), Point))
					Map.Diamond(Point, 3.0, TeamColor(Squad->GetTeamIndex()));
			}
		}
	}
}

void DrawRoute(const FMap& Map, const ForceRoutePresentation::FRoute& Route)
{
	const FLinearColor Color = Route.Color.CopyWithNewOpacity(Route.OrderIndex > 0 ? .6f : 1.f);
	const float Width = Route.bSelected || Route.bPreview ? 2.f : 1.f;
	for (int32 Index = 1; Index < Route.Line.Count; ++Index)
	{
		const FVector2D A = Map.Project(Route.Line.Points[Index - 1]), B = Map.Project(Route.Line.Points[Index]);
		const FVector2D Direction = (B - A).GetSafeNormal();
		const FVector2D Side(-Direction.Y, Direction.X);
		if (Route.bPreview || Route.OrderIndex > 0)
		{
			const double Length = FVector2D::Distance(A, B);
			for (double Offset = 0.; Offset < Length; Offset += 7.)
				Map.Line(A + Direction * Offset, A + Direction * FMath::Min(Offset + 4., Length), Color, Width);
		}
		else
			Map.Line(A, B, Color, Width);
		const FVector2D Tip = FVector2D::DistSquared(A, B) > 100. ? FMath::Lerp(A, B, .6) : B;
		Map.Line(Tip, Tip - Direction * 5. + Side * 3., Color, Width);
		Map.Line(Tip, Tip - Direction * 5. - Side * 3., Color, Width);
	}
	if (Route.Line.Count > 0 && (Route.bSelected || Route.bPreview))
		Map.Diamond(Map.Project(Route.Line.Points[Route.Line.Count - 1]), Route.OrderIndex == 0 ? 7. : 4., Color);
}

void DrawSelectedFront(const FMap& Map, const ACommandPlayerController& Controller)
{
	const ACommandBuilding* Selected = Controller.GetSelectedBuilding();
	FVector2D Front;
	if (IsValid(Selected) && Selected->IsAlive() && IsValid(Selected->ForceGroup)
		&& Map.Point(Selected->ForceGroup->Destination, Front))
	{
		Map.Diamond(Front, 6.0, Contested);
		Map.Line(Front - FVector2D(3, 0), Front + FVector2D(3, 0), Contested);
		Map.Line(Front - FVector2D(0, 3), Front + FVector2D(0, 3), Contested);
	}
}

void DrawPings(const FMap& Map, const ACommandGameState& State, const ACommandPlayerController& Controller)
{
	if (!Controller.PingCommands)
		return;
	const float Now = State.GetServerWorldTimeSeconds();
	for (const FObjectiveEvent& Event : Controller.PingCommands->GetEvents())
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

void DrawCaption(UCanvas* Canvas, const ACommandPlayerController& Controller, FVector2D Origin, float Size)
{
	if (!GEngine || !GEngine->GetSmallFont())
		return;
	const float TextScale = FMath::Clamp(Size / 210.f, .65f, 1.f);
	const FSlateFontInfo Font(GEngine->GetSmallFont(), 9.f * TextScale, FName(TEXT("Regular")));
	const bool bFortify = Controller.IsFortifyTargeting();
	const FSlateFontInfo HeaderFont(GEngine->GetSmallFont(),
		bFortify ? 8.f * FMath::Clamp(Size / 144.f, .78f, 1.f) : 9.f * TextScale,
		FName(bFortify ? TEXT("Bold") : TEXT("Regular")));
	FCanvasTextStringViewItem Header(Origin - FVector2D(0, 29.f * TextScale),
		FStringView(bFortify                    ? TEXT("LMB FORTIFY / RMB CANCEL")
				: Controller.IsAssigningOrder() ? TEXT("ARENA / LMB ATTACK / RMB CANCEL")
												: TEXT("ARENA / LMB PAN / RMB ORDER")),
		HeaderFont, bFortify ? FLinearColor(.46f, .94f, .56f) : View);
	Canvas->DrawItem(Header);
	const ACommandGameState* State = Controller.GetWorld() ? Controller.GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	const bool bOfflineHq = State && ((IsValid(State->FriendlyHeadquarters) && State->FriendlyHeadquarters->IsOffline()) || (IsValid(State->EnemyHeadquarters) && State->EnemyHeadquarters->IsOffline()));
	// While an HQ is offline the legend names its mark in place of the sector outline.
	FCanvasTextStringViewItem Legend(Origin - FVector2D(0, 15.f * TextScale),
		FStringView(bOfflineHq ? TEXT("HQ/base | X: HQ offline | amber: contest/front") : TEXT("HQ/base | sector | amber: contest/front")),
		Font, Neutral);
	Canvas->DrawItem(Legend);
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
	const double X = Extent.X - UV.Y * (2.0 * Extent.X), Y = UV.X * (2.0 * Extent.Y) - Extent.Y;
	const UWorld* World = Arena->GetWorld();
	OutWorld = FVector(X, Y, World ? GroundHeight::At(*World, X, Y) : 0.0);
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
	DrawBackdrop(Map, Origin, Size);
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	if (State)
	{
		DrawRegionOutlines(Map, *State, *Controller);
		DrawSites(Map, *State, *Controller);
		DrawStructures(Map, *State, *Controller);
	}
	DrawForces(Map, *World);
	if (State)
		DrawJevBadges(Map, *State);
	if (State)
		DrawFortifyRings(Map, *State);
	ForceRoutePresentation::Visit(*Controller, [&Map](const ForceRoutePresentation::FRoute& Route) { DrawRoute(Map, Route); });
	DrawSelectedFront(Map, *Controller);
	if (State)
		DrawPings(Map, *State, *Controller);
	Map.Box(Origin + FVector2D(Size, Size) * .5, Size * .5, GridColor);
	DrawFootprint(Map, Controller);
	DrawCaption(Canvas, *Controller, Origin, Size);
}
