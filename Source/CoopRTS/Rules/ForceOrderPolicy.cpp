#include "Rules/ForceOrderPolicy.h"

namespace
{
bool ValidGraph(const uint64* Neighbours, int32 RegionCount)
{
	return Neighbours && RegionCount > 0 && RegionCount <= ForceOrders::MaxRegions;
}

bool ValidRegion(int32 Region, int32 RegionCount)
{
	return Region >= 0 && Region < RegionCount;
}
}


uint64 ForceOrders::ConnectedMask(const uint64* Neighbours, int32 RegionCount, int32 Home, uint64 ControlledMask)
{
	if (!ValidGraph(Neighbours, RegionCount) || !ValidRegion(Home, RegionCount))
		return 0;
	const uint64 HomeBit = uint64(1) << Home;
	if (!(ControlledMask & HomeBit))
		return 0;
	uint64 Connected = HomeBit;
	int32 Queue[MaxRegions];
	int32 Read = 0, Write = 0;
	Queue[Write++] = Home;
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < RegionCount; ++Next)
		{
			const uint64 Bit = uint64(1) << Next;
			if (!(Neighbours[Current] & ControlledMask & Bit) || (Connected & Bit))
				continue;
			Connected |= Bit;
			Queue[Write++] = Next;
		}
	}
	return Connected;
}

int32 ForceOrders::SafeRegion(const uint64* Neighbours, int32 RegionCount, int32 Source, int32 Home,
	int32 LastHeld, uint64 ControlledMask, uint64 HostileMask)
{
	if (!ValidGraph(Neighbours, RegionCount)
		|| !ValidRegion(Source, RegionCount) || !ValidRegion(Home, RegionCount))
		return INDEX_NONE;
	const uint64 Safe = ConnectedMask(Neighbours, RegionCount, Home, ControlledMask) & ~HostileMask;
	if (!Safe)
		return Home;

	int32 Distance[MaxRegions];
	for (int32 Index = 0; Index < RegionCount; ++Index)
		Distance[Index] = INDEX_NONE;
	int32 Queue[MaxRegions];
	int32 Read = 0, Write = 0;
	Queue[Write++] = Source;
	Distance[Source] = 0;
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < RegionCount; ++Next)
		{
			if (!(Neighbours[Current] & (uint64(1) << Next)) || Distance[Next] != INDEX_NONE)
				continue;
			Distance[Next] = Distance[Current] + 1;
			Queue[Write++] = Next;
		}
	}
	if (ValidRegion(LastHeld, RegionCount) && (Safe & (uint64(1) << LastHeld))
		&& Distance[LastHeld] != INDEX_NONE)
		return LastHeld;
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < RegionCount; ++Index)
		if ((Safe & (uint64(1) << Index)) && Distance[Index] != INDEX_NONE
			&& (Best == INDEX_NONE || Distance[Index] < Distance[Best]))
			Best = Index;
	return Best == INDEX_NONE ? Home : Best;
}

bool ForceOrders::ShouldWithdraw(int32 Alive, int32 Capacity, uint8 Threshold)
{
	return Alive >= 0 && Capacity > 0 && Threshold <= 100
		&& static_cast<int64>(Alive) * 100 < static_cast<int64>(Capacity) * Threshold;
}

int32 ForceOrders::ResumeCount(int32 Capacity)
{
	return Capacity > 0 ? static_cast<int32>((static_cast<int64>(Capacity) * 4 + 4) / 5) : 0;
}

bool ForceOrders::ShouldResume(int32 Joined, int32 Capacity)
{
	return Capacity > 0 && Joined >= ResumeCount(Capacity);
}

bool ForceOrders::CanQueue(int32 OrderCount, bool bQueue)
{
	return OrderCount >= 0 && (!bQueue || OrderCount < 3);
}

float ForceOrders::SlowestSpeed(TConstArrayView<float> Speeds)
{
	if (Speeds.IsEmpty())
		return 0.f;
	float Slowest = 0.f;
	for (const float Speed : Speeds)
	{
		if (!FMath::IsFinite(Speed) || Speed < 0.f)
			return 0.f;
		if (Speed > 0.f)
			Slowest = Slowest > 0.f ? FMath::Min(Slowest, Speed) : Speed;
	}
	return Slowest;
}

float ForceOrders::TravelSpeed(float Base, bool bRetreat)
{
	return FMath::IsFinite(Base) && Base >= 0.f ? Base * (bRetreat ? 1.25f : 1.f) : 0.f;
}
