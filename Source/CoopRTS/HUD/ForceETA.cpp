#include "ForceETA.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "Rules/ForceCardPolicy.h"
#include "ArenaBounds.h"

namespace ForceTravelETA
{
int32 Compute(const AArmyGroup& Force, const ACommandGameState& State)
{
	if (Force.Status != EForceStatus::Marching && Force.Status != EForceStatus::Withdrawing && Force.Status != EForceStatus::Retreating)
		return INDEX_NONE;
	// The executor already applies the slowest living member, selection cap and Retreat sprint.
	const float Speed = Force.GetMarchSpeed();
	if (Speed <= 0.f)
		return INDEX_NONE;
	double Length = 0.;
	FVector Start = Force.GetCenter();
	auto Segment = [&](const FVector& End) {
		if (!AArenaBounds::IsTravelLocation(Force.GetWorld(), End))
			return false;
		Length += FVector::Dist2D(Start, End);
		Start = End;
		return true;
	};
	if (!Segment(Force.Destination))
		return INDEX_NONE;
	if (Force.Status == EForceStatus::Marching)
	{
		uint64 Graph[ForceOrders::MaxRegions];
		const int32 Count = ForceOrderGraph::ReadGraph(State, Graph);
		int32 Current = Force.WaypointRegionIndex;
		for (int32 Steps = 0; Current != Force.TargetRegionIndex && Steps < Count; ++Steps)
		{
			const int32 Next = ForceOrders::NextWaypoint(Graph, Count, Current, Force.TargetRegionIndex);
			if (Next == INDEX_NONE || Next == Current || !Segment(State.GetRegionAnchor(Next)))
				return INDEX_NONE;
			Current = Next;
		}
		if (Current != Force.TargetRegionIndex)
			return INDEX_NONE;
		if (IsValid(Force.TargetStructure) && !Segment(Force.TargetStructure->GetActorLocation()))
			return INDEX_NONE;
	}
	return ForceCardPolicy::TravelSeconds(Length, Speed);
}
}
