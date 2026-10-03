#include "HUD/ForceRoutePresentation.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/OrderGraph.h"
#include "Engine/Level.h"
#include "Engine/World.h"
#include "MapRegion.h"
#include "WorldOverlay.h"
#include <limits>

static void VisitOrders(const AArmyGroup& Force, bool bSelected, TConstArrayView<FVector> Anchors,
	TFunctionRef<void(const ForceRoutePresentation::FRoute&)> Draw)
{
	const FLinearColor Color = AArmyUnit::GetCommanderColor(Force.GetOwningPlayerState()->CommanderIndex);
	FVector Start = Force.GetCenter();
	for (int32 Leg = 0; Leg < Force.GetIntentRoutes().Num(); ++Leg)
	{
		const FForceRoute& Route = Force.GetIntentRoutes()[Leg];
		if (Route.Regions.IsEmpty())
			break;
		ForceRoutePresentation::FRoute Render;
		Render.OrderIndex = Route.OrderIndex;
		Render.TargetRegionIndex = Route.Regions.Last();
		Render.bActive = Leg == 0;
		Render.bSelected = bSelected;
		Render.Color = Color;
		Render.Line = RouteIntent::Polyline(Start, Route.Regions, Anchors,
			Leg == 0 && Force.WaypointRegionIndex == Route.Regions[0]);
		if (Render.Line.Count == 0)
			break;
		// Accepted formation destinations and structure targets need not be the region anchor.
		if (Leg == 0 && Route.Regions.Last() == Force.WaypointRegionIndex)
			Render.Line.Points[Render.Line.Count - 1] = Force.Destination;
		if (Force.Orders.IsValidIndex(Route.OrderIndex))
		{
			const FForceOrder& Order = Force.Orders[Route.OrderIndex];
			if (IsValid(Order.Structure) && Route.Regions.Last() == Order.RegionIndex)
				Render.Line.Points[Render.Line.Count - 1] = Order.Structure->GetActorLocation();
		}
		Draw(Render);
		Start = Render.Line.Points[Render.Line.Count - 1];
	}
}

void ForceRoutePresentation::Visit(const ACommandPlayerController& Controller, TFunctionRef<void(const FRoute&)> Draw)
{
	const ACommandGameState* State = Controller.GetWorld()->GetGameState<ACommandGameState>();
	const ACommandPlayerState* Viewer = Controller.GetPlayerState<ACommandPlayerState>();
	if (!State || !Viewer || Controller.GetUIScreen() != ECommandScreen::Game)
		return;
	FVector Anchors[ForceOrders::MaxRegions];
	for (FVector& Anchor : Anchors)
		Anchor = FVector(std::numeric_limits<double>::quiet_NaN());
	for (const AMapRegion* Region : State->Regions)
		if (IsValid(Region) && Region->RegionIndex >= 0 && Region->RegionIndex < ForceOrders::MaxRegions)
			Anchors[Region->RegionIndex] = State->GetRegionAnchor(Region->RegionIndex);
	FForceOrder Hover;
	bool bQueue = false;
	const bool bPreview = Controller.GetHoveredForceOrder(Hover, bQueue);
	uint64 Graph[ForceOrders::MaxRegions];
	const int32 Count = bPreview ? ForceOrderGraph::ReadGraph(*State, Graph) : 0;
	for (const ULevel* Level : Controller.GetWorld()->GetLevels())
	{
		if (!Level)
			continue;
		for (const AActor* Actor : Level->Actors)
		{
			const AArmyGroup* Force = Cast<AArmyGroup>(Actor);
			const ACommandPlayerState* Owner = IsValid(Force) ? Force->GetOwningPlayerState() : nullptr;
			if (!IsValid(Owner) || Owner->CommanderIndex < 0 || Force->GetTeamIndex() != Viewer->TeamIndex
				|| (Force->GetAliveCount() == 0 && !IsValid(Force->GetProductionBuilding())))
				continue;
			const bool bSelected = Controller.IsForceSelected(Force);
			VisitOrders(*Force, bSelected, MakeArrayView(Anchors), Draw);
			if (!bSelected || !bPreview)
				continue;
			int32 Source = ForceOrderGraph::SourceRegion(*Force, *State);
			FVector Start = Force->GetCenter();
			if (bQueue && !Force->GetIntentRoutes().IsEmpty())
			{
				const FForceRoute& Last = Force->GetIntentRoutes().Last();
				if (Last.Regions.IsEmpty())
					continue;
				Source = Last.Regions.Last();
				Start = Anchors[Source];
			}
			const RouteIntent::FPath Path = RouteIntent::Path(Graph, Count, Source, Hover.RegionIndex);
			FRoute Render;
			const AMapRegion* Current = ForceOrderGraph::Region(*State, Source);
			Render.Line = RouteIntent::Polyline(Start, MakeArrayView(Path.Regions, Path.Count), MakeArrayView(Anchors),
				Current && Current->Anchor && State->GetRegionController(Source) != Force->GetTeamIndex());
			if (Render.Line.Count == 0)
				continue;
			if (IsValid(Hover.Structure))
				Render.Line.Points[Render.Line.Count - 1] = Hover.Structure->GetActorLocation();
			Render.Color = FLinearColor::White;
			Render.bSelected = Render.bPreview = true;
			Render.OrderIndex = bQueue ? Force->Orders.Num() : 0;
			Render.TargetRegionIndex = Hover.RegionIndex;
			Draw(Render);
		}
	}
}

void ForceRoutePresentation::DrawWorld(AWorldOverlay& Overlay, const ACommandPlayerController& Controller)
{
	const ACommandGameState* State = Controller.GetWorld()->GetGameState<ACommandGameState>();
	Visit(Controller, [&Overlay, State](const FRoute& Route) {
		const FColor Color = Route.Color.ToFColor(true);
		const float Width = Route.bPreview ? 9.f : Route.bSelected ? 7.f
																   : 4.f;
		const FVector Lift(0., 0., Route.bPreview ? 24. : 18.);
		for (int32 Index = 1; Index < Route.Line.Count; ++Index)
		{
			const FVector A = Route.Line.Points[Index - 1] + Lift, B = Route.Line.Points[Index] + Lift;
			const FVector Direction = (B - A).GetSafeNormal2D();
			const FVector Side(-Direction.Y, Direction.X, 0.);
			if (Route.bPreview || Route.OrderIndex > 0)
			{
				const double Length = FVector::Dist2D(A, B);
				for (double Offset = 0.; Offset < Length; Offset += 130.)
					Overlay.Line(A + Direction * Offset, A + Direction * FMath::Min(Offset + 80., Length), Color, Width);
			}
			else
				Overlay.Line(A, B, Color, Width);
			// Keep direction visible when the destination sits beneath a structure.
			const FVector Tip = FVector::DistSquared2D(A, B) > FMath::Square(180.) ? FMath::Lerp(A, B, .6) : B;
			Overlay.Line(Tip, Tip - Direction * 90. + Side * 45., Color, Width);
			Overlay.Line(Tip, Tip - Direction * 90. - Side * 45., Color, Width);
		}
		if (Route.Line.Count > 0 && (Route.bSelected || Route.bPreview))
			Overlay.Ring(Route.Line.Points[Route.Line.Count - 1] + Lift, Route.OrderIndex == 0 ? 110.f : 65.f, Color);
		if (State && ((Route.bSelected && Route.bActive) || Route.bPreview))
			if (const AMapRegion* Region = ForceOrderGraph::Region(*State, Route.TargetRegionIndex))
			{
				const double Z = State->GetRegionAnchor(Region->RegionIndex).Z + Lift.Z;
				for (int32 Index = 0; Index < Region->Polygon.Num(); ++Index)
				{
					const FVector2D& A = Region->Polygon[Index];
					const FVector2D& B = Region->Polygon[(Index + 1) % Region->Polygon.Num()];
					Overlay.Line(FVector(A.X, A.Y, Z), FVector(B.X, B.Y, Z), Color, Width);
				}
			}
	});
}
