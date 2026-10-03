#include "Algo/Compare.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/OrderGraph.h"
#include "Rules/RouteIntent.h"
#include "Engine/World.h"

namespace
{
// Writes published legs in place and tracks whether any leg differs from what clients hold.
struct FRouteWriter
{
	TArray<FForceRoute>& Routes;
	const uint64* Graph;
	int32 Count;
	int32 Source;
	int32 Leg = 0;
	bool bChanged = false;

	bool Publish(TConstArrayView<int32> Path, int32 OrderIndex)
	{
		if (Path.IsEmpty())
			return false;
		if (!Routes.IsValidIndex(Leg))
			Routes.AddDefaulted();
		FForceRoute& Route = Routes[Leg++];
		if (Route.OrderIndex != OrderIndex || Route.Regions.Num() != Path.Num()
			|| !Algo::Compare(Route.Regions, Path))
		{
			Route.OrderIndex = OrderIndex;
			Route.Regions.Reset(Path.Num());
			Route.Regions.Append(Path.GetData(), Path.Num());
			bChanged = true;
		}
		Source = Path.Last();
		return true;
	}

	bool Append(int32 Target, int32 OrderIndex, bool bDirect)
	{
		if (bDirect)
		{
			if (Source < 0 || Source >= Count || Target < 0 || Target >= Count)
				return false;
			const int32 Regions[] = { Source, Target };
			return Publish(MakeArrayView(Regions, Source == Target ? 1 : 2), OrderIndex);
		}
		const RouteIntent::FPath Path = RouteIntent::Path(Graph, Count, Source, Target);
		return Publish(MakeArrayView(Path.Regions, Path.Count), OrderIndex);
	}

	// Drops legs beyond those just published.
	void Trim()
	{
		if (Routes.Num() == Leg)
			return;
		Routes.SetNum(Leg, EAllowShrinking::No);
		bChanged = true;
	}
};
}

void AArmyGroup::UpdateIntentRoutes(const uint64* Graph, int32 Count, int32 Source, uint64 Controlled, uint64 Hostiles, bool bTargetCompleted)
{
	if (!IsValid(OwningPlayerState) || OwningPlayerState->CommanderIndex < 0 || TeamIndex == 5)
	{
		if (!IntentRoutes.IsEmpty())
		{
			IntentRoutes.Reset();
			ForceNetUpdate();
		}
		return;
	}
	const ACommandGameState* State = GetWorld()->GetGameState<ACommandGameState>();
	if (!State)
		return;
	IntentRoutes.Reserve(4);
	FRouteWriter Writer{ IntentRoutes, Graph, Count, Source };
	int32 PredictedLastHeld = LastHeldRegionIndex;
	const bool bDropAttack = bWithdrawing && (!IsValid(ProductionBuilding) || bTargetCompleted);
	if ((bWithdrawing || Verb == EForceVerb::Retreat) && !Writer.Append(WithdrawalRegionIndex, 0, true))
		Writer.Leg = 0;
	else
		for (int32 Index = Verb == EForceVerb::Retreat || bDropAttack ? 1 : 0; Index < Orders.Num(); ++Index)
		{
			const FForceOrder& Entry = Orders[Index];
			const int32 Target = Entry.Verb == EForceVerb::Retreat
				? ForceOrders::SafeRegion(Graph, Count, Writer.Source, ForceOrderGraph::TeamMain(*State, TeamIndex), PredictedLastHeld, Controlled, Hostiles)
				: Entry.RegionIndex;
			if (!Writer.Append(Target, Index, Entry.Verb == EForceVerb::Retreat))
				break;
			if (Entry.Verb == EForceVerb::MoveHold)
				PredictedLastHeld = Target;
		}
	Writer.Trim();
	if (Writer.bChanged)
		ForceNetUpdate();
}
