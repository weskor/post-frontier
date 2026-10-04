#include "Rules/SupplyDeliveryPolicy.h"

#include "Rules/ForceOrderPolicy.h"

double SupplyDelivery::Delay(int32 Hops)
{
	return BaseDelaySeconds + HopDelaySeconds * FMath::Max(0, Hops);
}

int32 SupplyDelivery::Hops(const uint64* Neighbours, int32 RegionCount, uint64 Connected, int32 From, int32 To)
{
	if (!Neighbours || RegionCount <= 0 || RegionCount > ForceOrders::MaxRegions
		|| From < 0 || From >= RegionCount || To < 0 || To >= RegionCount
		|| !(Connected & (uint64(1) << From)) || !(Connected & (uint64(1) << To)))
		return INDEX_NONE;
	int32 Distance[ForceOrders::MaxRegions];
	for (int32 Index = 0; Index < RegionCount; ++Index)
		Distance[Index] = INDEX_NONE;
	int32 Queue[ForceOrders::MaxRegions];
	int32 Read = 0, Write = 0;
	Queue[Write++] = From;
	Distance[From] = 0;
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < RegionCount; ++Next)
		{
			const uint64 Bit = uint64(1) << Next;
			if (!(Neighbours[Current] & Connected & Bit) || Distance[Next] != INDEX_NONE)
				continue;
			Distance[Next] = Distance[Current] + 1;
			Queue[Write++] = Next;
		}
	}
	return Distance[To];
}

SupplyDelivery::ERoute SupplyDelivery::Route(bool bForceEmpty, int32 Hops)
{
	if (bForceEmpty)
		return ERoute::Empty;
	return Hops == INDEX_NONE ? ERoute::CutOff : ERoute::Connected;
}

SupplyDelivery::EOutcome SupplyDelivery::Advance(FRecruit& Recruit, ERoute Route, int32 Hops, double Now)
{
	if (Route == ERoute::Empty)
	{
		Recruit.bWaiting = true;
		return EOutcome::AtProducer;
	}
	if (Route == ERoute::CutOff)
	{
		Recruit.bWaiting = true;
		return EOutcome::Pending;
	}
	if (Recruit.bWaiting)
	{
		Recruit.bWaiting = false;
		Recruit.ReadyAt = Now + Delay(Hops);
		return EOutcome::Pending;
	}
	return Now >= Recruit.ReadyAt ? EOutcome::Arrive : EOutcome::Pending;
}

SupplyDelivery::FCounts SupplyDelivery::Count(TConstArrayView<FRecruit> Recruits)
{
	FCounts Counts;
	for (const FRecruit& Recruit : Recruits)
	{
		if (Recruit.bWaiting)
			++Counts.Waiting;
		else
			++Counts.InTransit;
	}
	return Counts;
}

int32 SupplyDelivery::Refund(TConstArrayView<FRecruit> Recruits)
{
	int32 Total = 0;
	for (const FRecruit& Recruit : Recruits)
		Total += FMath::Max(0, Recruit.Paid);
	return Total;
}
