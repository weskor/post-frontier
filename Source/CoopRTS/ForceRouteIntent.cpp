#include "Algo/Compare.h"
#include "ArmyGroup.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/OrderGraph.h"
#include "Rules/RouteIntent.h"
#include "Engine/World.h"

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
	int32 Leg = 0;
	bool bChanged = false;
	auto Publish = [&](TConstArrayView<int32> Path, int32 OrderIndex) {
		if (Path.IsEmpty())
			return false;
		if (!IntentRoutes.IsValidIndex(Leg))
			IntentRoutes.AddDefaulted();
		FForceRoute& Route = IntentRoutes[Leg++];
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
	};
	auto Append = [&](int32 Target, int32 OrderIndex, bool bDirect) {
		if (bDirect)
		{
			if (Source < 0 || Source >= Count || Target < 0 || Target >= Count)
				return false;
			const int32 Regions[] = { Source, Target };
			return Publish(MakeArrayView(Regions, Source == Target ? 1 : 2), OrderIndex);
		}
		const RouteIntent::FPath Path = RouteIntent::Path(Graph, Count, Source, Target);
		return Publish(MakeArrayView(Path.Regions, Path.Count), OrderIndex);
	};
	int32 PredictedLastHeld = LastHeldRegionIndex;
	const bool bDropAttack = bWithdrawing && (!IsValid(ProductionBuilding) || bTargetCompleted);
	if ((bWithdrawing || Verb == EForceVerb::Retreat) && !Append(WithdrawalRegionIndex, 0, true))
		Leg = 0;
	else
		for (int32 Index = Verb == EForceVerb::Retreat || bDropAttack ? 1 : 0; Index < Orders.Num(); ++Index)
		{
			const FForceOrder& Entry = Orders[Index];
			const int32 Target = Entry.Verb == EForceVerb::Retreat
				? ForceOrders::SafeRegion(Graph, Count, Source, ForceOrderGraph::TeamMain(*State, TeamIndex), PredictedLastHeld, Controlled, Hostiles)
				: Entry.RegionIndex;
			if (!Append(Target, Index, Entry.Verb == EForceVerb::Retreat))
				break;
			if (Entry.Verb == EForceVerb::MoveHold)
				PredictedLastHeld = Target;
		}
	if (IntentRoutes.Num() != Leg)
	{
		IntentRoutes.SetNum(Leg, EAllowShrinking::No);
		bChanged = true;
	}
	if (bChanged)
		ForceNetUpdate();
}
