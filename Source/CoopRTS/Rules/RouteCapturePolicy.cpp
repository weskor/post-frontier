#include "Rules/RouteCapturePolicy.h"

#include "Rules/RouteIntent.h"

namespace
{
bool OnRoute(const uint64* Graph, int32 Count, const RouteCapturePolicy::FMarch& March)
{
	if (March.Origin == INDEX_NONE || March.Applied == INDEX_NONE)
		return true;
	const RouteIntent::FPath Route = RouteIntent::Path(Graph, Count, March.Origin, March.Target);
	if (Route.Count == 0)
		return true;
	for (int32 Index = 0; Index < Route.Count; ++Index)
		if (Route.Regions[Index] == March.Source)
			return true;
	return false;
}
}

RouteCapturePolicy::FDecision RouteCapturePolicy::Decide(const uint64* Graph, int32 Count, const FMarch& March)
{
	if (!OnRoute(Graph, Count, March))
		return { EStep::Continue, March.Origin };
	const bool bAdvance = March.Source == March.Target
		|| !March.bHasAnchor || March.bControlled
		|| (March.bContested && March.Applied != INDEX_NONE && (March.Applied != March.Source || March.bArrived));
	return { bAdvance ? EStep::Advance : EStep::Secure, March.Source };
}
