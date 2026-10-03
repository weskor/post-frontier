#include "ForceETA.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "Rules/ForceCardPolicy.h"
#include "ForceRoutePresentation.h"

namespace ForceTravelETA
{
int32 Compute(const AArmyGroup& Force, const ACommandGameState& State)
{
	if (Force.Status != EForceStatus::Marching && Force.Status != EForceStatus::Withdrawing && Force.Status != EForceStatus::Retreating)
		return INDEX_NONE;
	// No living member means no slowest member to time; the card shows no countdown.
	if (Force.GetAliveCount() <= 0)
		return INDEX_NONE;
	// The executor already applies the slowest living member, selection cap and Retreat sprint.
	const float Speed = Force.GetMarchSpeed();
	if (Speed <= 0.f)
		return INDEX_NONE;
	const TArray<FForceRoute>& Routes = Force.GetIntentRoutes();
	if (Routes.IsEmpty())
		return INDEX_NONE;
	const FForceRoute& Route = Routes[0];
	FVector Anchors[ForceOrders::MaxRegions];
	for (int32 Region : Route.Regions)
	{
		if (Region < 0 || Region >= ForceOrders::MaxRegions || !ForceOrderGraph::Region(State, Region))
			return INDEX_NONE;
		Anchors[Region] = State.GetRegionAnchor(Region);
	}
	const RouteIntent::FPolyline Line = ForceRoutePresentation::BuildLine(Force, Route, Force.GetCenter(), true, Anchors);
	if (Line.Count == 0)
		return INDEX_NONE;
	double Length = 0.;
	for (int32 Index = 1; Index < Line.Count; ++Index)
		Length += FVector::Dist2D(Line.Points[Index - 1], Line.Points[Index]);
	return ForceCardPolicy::TravelSeconds(Length, Speed);
}
}
