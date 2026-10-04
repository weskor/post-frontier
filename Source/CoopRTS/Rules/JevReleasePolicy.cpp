#include "JevReleasePolicy.h"

#include "EconomyPolicy.h"

namespace JevRelease
{
namespace
{
constexpr int32 WaveBudgets[ScheduledReleases] = { 0, 150, 250, 400, 600 };
constexpr int32 ArmorClasses = 4;

int32 ArmorSlot(EArmorClass Armor)
{
	return static_cast<int32>(Armor);
}

// Cheapest option with a positive cost, optionally limited to those strong against Target.
int32 CheapestOption(TConstArrayView<FUnitOption> Options, bool bOnlyCounters, EArmorClass Target)
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Options.Num(); ++Index)
	{
		const FUnitOption& Option = Options[Index];
		if (Option.Cost <= 0 || (bOnlyCounters && !CombatPolicy::IsStrongAgainst(Option.Damage, Target)))
			continue;
		if (Best == INDEX_NONE || Option.Cost < Options[Best].Cost)
			Best = Index;
	}
	return Best;
}

void Buy(FPurchase& Out, TConstArrayView<FUnitOption> Options, int32 Option)
{
	if (Option == INDEX_NONE)
		return;
	const int32 Count = Out.Carry / Options[Option].Cost;
	Out.Counts[Option] += Count;
	Out.Units += Count;
	Out.Spent += Count * Options[Option].Cost;
	Out.Carry -= Count * Options[Option].Cost;
}

// Fewest hops from Home through every existing region, ascending-index breadth-first.
void HopsFrom(const JevPlanner::FWorld& World, int32 Home, int32 (&Hops)[ForceOrders::MaxRegions])
{
	for (int32& Hop : Hops)
		Hop = INDEX_NONE;
	int32 Queue[ForceOrders::MaxRegions];
	int32 Read = 0, Write = 0;
	Queue[Write++] = Home;
	Hops[Home] = 0;
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < ForceOrders::MaxRegions; ++Next)
			if (World.Regions[Next].bExists && Hops[Next] == INDEX_NONE
				&& (World.Regions[Current].Neighbours & (uint64(1) << Next)))
			{
				Hops[Next] = Hops[Current] + 1;
				Queue[Write++] = Next;
			}
	}
}

bool Nearer(const JevPlanner::FWorld& World, const int32 (&Hops)[ForceOrders::MaxRegions], int32 A, int32 B)
{
	if (B == INDEX_NONE || Hops[A] != Hops[B])
		return B == INDEX_NONE || Hops[A] < Hops[B];
	const FVector& Home = World.Regions[World.Home].Position;
	const double DistA = FVector::DistSquared2D(Home, World.Regions[A].Position);
	const double DistB = FVector::DistSquared2D(Home, World.Regions[B].Position);
	return DistA < DistB;
}
}

float ReleaseTime(int32 Index)
{
	static constexpr float Times[ScheduledReleases] = { 0.f, 120.f, 240.f, 360.f, 480.f };
	if (Index < 0)
		return -1.f;
	return Index < ScheduledReleases ? Times[Index]
									 : OverrunStartSeconds + (Index - ScheduledReleases) * OverrunIntervalSeconds;
}

EVersion VersionOf(int32 Index)
{
	return Index < ScheduledReleases ? static_cast<EVersion>(FMath::Max(0, Index)) : EVersion::Overrun;
}

int32 IndexAt(float Seconds)
{
	if (Seconds < 0.f)
		return INDEX_NONE;
	if (Seconds < OverrunStartSeconds)
	{
		int32 Index = 0;
		while (Index + 1 < ScheduledReleases && ReleaseTime(Index + 1) <= Seconds)
			++Index;
		return Index;
	}
	return ScheduledReleases + FMath::FloorToInt((Seconds - OverrunStartSeconds) / OverrunIntervalSeconds);
}

bool TimelineVisible(int32 Index, float Seconds)
{
	return Index >= 0 && Seconds >= ReleaseTime(Index) - TimelineLeadSeconds;
}

int32 BaseBudget(int32 Index)
{
	if (Index < 0)
		return 0;
	return Index < ScheduledReleases ? WaveBudgets[Index] : OverrunBudget;
}

int32 WaveBudget(int32 Index, int32 HumanCommanders)
{
	return FMath::RoundToInt(BaseBudget(Index) * EconomyPolicy::JevPlayerCountFactor(HumanCommanders));
}

FBehaviour BehaviourFor(int32 Index)
{
	const EVersion Version = VersionOf(Index);
	FBehaviour Behaviour;
	Behaviour.bRaid = Version >= EVersion::V11;
	Behaviour.bCounter = Version >= EVersion::V12;
	Behaviour.bCoordinated = Version >= EVersion::V20;
	Behaviour.SpeedFactor = Version >= EVersion::V21 ? RapidSpeedFactor : 1.f;
	return Behaviour;
}

EArmorClass MostNumerous(const FArmorCounts& Counts)
{
	int32 Best = INDEX_NONE;
	for (int32 Slot = 0; Slot < ArmorClasses; ++Slot)
		if (Counts.Count[Slot] > 0 && (Best == INDEX_NONE || Counts.Count[Slot] > Counts.Count[Best]))
			Best = Slot;
	return Best == INDEX_NONE ? EArmorClass::Unset : static_cast<EArmorClass>(Best);
}

FPurchase Purchase(int32 Pool, TConstArrayView<FUnitOption> Options, const FBehaviour& Behaviour, EArmorClass Target)
{
	FPurchase Out;
	Out.Counts.SetNumZeroed(Options.Num());
	Out.Carry = FMath::Max(0, Pool);
	const int32 Cheapest = CheapestOption(Options, false, Target);
	const bool bCounter = Behaviour.bCounter && ArmorSlot(Target) >= 0 && ArmorSlot(Target) < ArmorClasses;
	const int32 Counter = bCounter ? CheapestOption(Options, true, Target) : INDEX_NONE;
	Buy(Out, Options, Counter);
	Buy(Out, Options, Cheapest);
	return Out;
}

TArray<int32> SplitForces(int32 UnitCount)
{
	TArray<int32> Sizes;
	if (UnitCount <= 0)
		return Sizes;
	const int32 Forces = FMath::DivideAndRoundUp(UnitCount, MaxForceSize);
	for (int32 Index = 0; Index < Forces; ++Index)
		Sizes.Add(UnitCount / Forces + (Index < UnitCount % Forces ? 1 : 0));
	return Sizes;
}

int32 RaidRegion(const JevPlanner::FWorld& World)
{
	const bool bHomeKnown = World.Home >= 0 && World.Home < ForceOrders::MaxRegions && World.Regions[World.Home].bExists;
	if (!bHomeKnown || World.EnemyHome < 0 || World.EnemyHome >= ForceOrders::MaxRegions)
		return World.EnemyHome;
	uint64 Hostile = 0;
	uint64 Neighbours[ForceOrders::MaxRegions] = {};
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
	{
		const JevPlanner::FRegion& Region = World.Regions[Index];
		Neighbours[Index] = Region.Neighbours;
		if (Region.bExists && Region.Controller != INDEX_NONE && Region.Controller != World.Team)
			Hostile |= uint64(1) << Index;
	}
	const uint64 Connected = ForceOrders::ConnectedMask(Neighbours, ForceOrders::MaxRegions, World.EnemyHome, Hostile);
	int32 Hops[ForceOrders::MaxRegions];
	HopsFrom(World, World.Home, Hops);
	int32 Rigs = INDEX_NONE, Any = INDEX_NONE;
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
	{
		if (!(Connected & (uint64(1) << Index)) || Hops[Index] == INDEX_NONE)
			continue;
		if (World.Regions[Index].HostileRigs > 0 && Nearer(World, Hops, Index, Rigs))
			Rigs = Index;
		if (Nearer(World, Hops, Index, Any))
			Any = Index;
	}
	return Rigs != INDEX_NONE ? Rigs : Any != INDEX_NONE ? Any : World.EnemyHome;
}
}
