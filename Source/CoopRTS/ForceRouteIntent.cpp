#include "Algo/Compare.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "Commands/OrderGraph.h"
#include "Rules/RouteIntent.h"
#include "Engine/World.h"

void AArmyGroup::UpdateIntentRoutes(const uint64* Graph, int32 Count, int32 Source, uint64 Controlled, uint64 Hostiles)
{
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return;
	IntentRoutes.Reserve(4);
	int32 Leg = 0;
	bool bChanged = false;
	auto Append = [&](int32 Target, int32 OrderIndex) {
		const RouteIntent::FPath Path = RouteIntent::Path(Graph, Count, Source, Target);
		if (Path.Count == 0)
			return false;
		if (!IntentRoutes.IsValidIndex(Leg))
			IntentRoutes.AddDefaulted();
		FForceRoute& Route = IntentRoutes[Leg++];
		const TConstArrayView<int32> View(Path.Regions, Path.Count);
		if (Route.OrderIndex != OrderIndex || Route.Regions.Num() != Path.Count
			|| !Algo::Compare(Route.Regions, View))
		{
			Route.OrderIndex = OrderIndex;
			Route.Regions.Reset(Path.Count);
			Route.Regions.Append(Path.Regions, Path.Count);
			bChanged = true;
		}
		Source = Target;
		return true;
	};
	if ((bWithdrawing || Verb == EForceVerb::Retreat) && !Append(WithdrawalRegionIndex, 0))
		Leg = 0;
	else
		for (int32 Index = Verb == EForceVerb::Retreat ? 1 : 0; Index < Orders.Num(); ++Index)
		{
			const FForceOrder& Entry = Orders[Index];
			const int32 Target = Entry.Verb == EForceVerb::Retreat
				? ForceOrders::SafeRegion(Graph, Count, Source, ForceOrderGraph::TeamMain(*State, TeamIndex), LastHeldRegionIndex, Controlled, Hostiles)
				: Entry.RegionIndex;
			if (!Append(Target, Index))
				break;
		}
	if (IntentRoutes.Num() != Leg)
	{
		IntentRoutes.SetNum(Leg, EAllowShrinking::No);
		bChanged = true;
	}
	if (bChanged)
		ForceNetUpdate();
}
