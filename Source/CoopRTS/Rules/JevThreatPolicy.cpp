#include "JevThreatPolicy.h"

#include "JevIntent.h"

namespace JevThreat
{
namespace
{
using JevPlanner::FWorld;
constexpr int32 MaxRegions = ForceOrders::MaxRegions;

bool ValidIndex(int32 Region)
{
	return Region >= 0 && Region < MaxRegions;
}

bool Exists(const FWorld& World, int32 Region)
{
	return ValidIndex(Region) && World.Regions[Region].bExists;
}

bool Neighbour(const FWorld& World, int32 From, int32 To)
{
	return (World.Regions[From].Neighbours & (uint64(1) << To)) != 0;
}

// Fewest hops from Source over existing regions, never entering Banned; INDEX_NONE when unreachable.
void Hops(const FWorld& World, int32 Source, int32 Banned, int32 (&Out)[MaxRegions])
{
	for (int32& Hop : Out)
		Hop = INDEX_NONE;
	if (!Exists(World, Source))
		return;
	int32 Queue[MaxRegions];
	int32 Read = 0, Write = 0;
	Queue[Write++] = Source;
	Out[Source] = 0;
	while (Read < Write)
	{
		const int32 Here = Queue[Read++];
		for (int32 There = 0; There < MaxRegions; ++There)
			if (There != Banned && Exists(World, There) && Out[There] == INDEX_NONE && Neighbour(World, Here, There))
			{
				Out[There] = Out[Here] + 1;
				Queue[Write++] = There;
			}
	}
}

bool HumansHold(const FWorld& World, int32 Region)
{
	const int32 Controller = World.Regions[Region].Controller;
	return Controller != INDEX_NONE && Controller != World.Team;
}

bool JevHolds(const FWorld& World, int32 Region)
{
	return World.Regions[Region].Controller == World.Team;
}

// Distinct, existing, non-adjacent supply necks.
bool WellFormed(const FWorld& World, const FPair& Pair)
{
	return Exists(World, Pair.A) && Exists(World, Pair.B) && Pair.A != Pair.B && !Neighbour(World, Pair.A, Pair.B)
		&& IsSupplyNeck(World, Pair.A) && IsSupplyNeck(World, Pair.B);
}
}

float PublishTime()
{
	return LaunchTime() - LeadSeconds;
}

float LaunchTime()
{
	return JevRelease::ReleaseTime(TriggerRelease);
}

EStep NextStep(EStage Stage, float MatchSeconds)
{
	switch (Stage)
	{
	case EStage::Waiting:
		return MatchSeconds >= PublishTime() ? EStep::Publish : EStep::None;
	case EStage::Published:
		return MatchSeconds >= LaunchTime() ? EStep::Launch : EStep::None;
	default:
		return EStep::None;
	}
}

int32 TargetCount(int32 HumanCommanders)
{
	return HumanCommanders >= CoopCommanders ? MaxTargets : 1;
}

int32 ForceBudget(int32 HumanCommanders)
{
	return JevRelease::WaveBudget(TriggerRelease, HumanCommanders) / 2;
}

int32 PairOfTag(FStringView Tag)
{
	const FStringView Prefix(TagPrefix);
	if (!Tag.StartsWith(Prefix, ESearchCase::CaseSensitive))
		return INDEX_NONE;
	const FStringView Digits = Tag.RightChop(Prefix.Len());
	if (Digits.IsEmpty() || Digits.Len() > 2)
		return INDEX_NONE;
	int32 Value = 0;
	for (const TCHAR Digit : Digits)
	{
		if (Digit < TEXT('0') || Digit > TEXT('9'))
			return INDEX_NONE;
		Value = Value * 10 + (Digit - TEXT('0'));
	}
	return Value < MaxPairs ? Value : INDEX_NONE;
}

TArray<FPair> BuildPairs(TConstArrayView<FTaggedRegion> Tagged)
{
	TArray<FPair> Pairs;
	for (int32 Pair = 0; Pair < MaxPairs; ++Pair)
	{
		FPair Found;
		int32 Count = 0;
		for (const FTaggedRegion& Entry : Tagged)
		{
			if (Entry.Pair != Pair)
				continue;
			if (Count == 0)
				Found.A = Entry.Region;
			else if (Count == 1)
				Found.B = Entry.Region;
			++Count;
		}
		if (Count == 2 && Found.A != Found.B)
			Pairs.Add(Found);
	}
	return Pairs;
}

bool Adjacent(const FWorld& World, int32 A, int32 B)
{
	return Exists(World, A) && Exists(World, B) && Neighbour(World, A, B);
}

bool IsSupplyNeck(const FWorld& World, int32 Region)
{
	if (!Exists(World, Region) || !Exists(World, World.Home) || !Exists(World, World.EnemyHome) || World.Regions[Region].bMain
		|| Region == World.Home || Region == World.EnemyHome)
		return false;
	int32 Human[MaxRegions], Jev[MaxRegions], Without[MaxRegions];
	Hops(World, World.EnemyHome, INDEX_NONE, Human);
	Hops(World, World.Home, INDEX_NONE, Jev);
	if (Human[Region] == INDEX_NONE || (Jev[Region] != INDEX_NONE && Human[Region] >= Jev[Region]))
		return false;
	Hops(World, World.EnemyHome, Region, Without);
	for (int32 Other = 0; Other < MaxRegions; ++Other)
		if (Other != Region && Human[Other] != INDEX_NONE && (Without[Other] == INDEX_NONE || Without[Other] > Human[Other]))
			return true;
	return false;
}

const TCHAR* SkipReason(ESkip Skip)
{
	switch (Skip)
	{
	case ESkip::NoPairs:
		return TEXT("the map authors no neck pair");
	case ESkip::NoEligiblePair:
		return TEXT("every authored pair is malformed or holds a region JEV controls");
	default:
		return TEXT("");
	}
}

FChoice ChoosePair(const FWorld& World, TConstArrayView<FPair> Pairs)
{
	FChoice Choice;
	if (Pairs.IsEmpty())
		return Choice;
	Choice.Skip = ESkip::NoEligiblePair;
	int32 Human[MaxRegions];
	Hops(World, World.EnemyHome, INDEX_NONE, Human);
	int32 BestHeld = -1, BestHops = MAX_int32;
	for (const FPair& Pair : Pairs)
	{
		if (!WellFormed(World, Pair) || JevHolds(World, Pair.A) || JevHolds(World, Pair.B))
			continue;
		const int32 Held = HumansHold(World, Pair.A) + HumansHold(World, Pair.B);
		const int32 Distance = Human[Pair.A] + Human[Pair.B];
		if (Held > BestHeld || (Held == BestHeld && Distance < BestHops))
		{
			BestHeld = Held;
			BestHops = Distance;
			Choice.Pair = Pair;
		}
	}
	if (BestHeld < 0)
		return Choice;
	Choice.Skip = ESkip::None;
	Choice.Held = BestHeld;
	Choice.bFallback = BestHeld < 2;
	return Choice;
}

FTargets ChooseTargets(const FWorld& World, const FPair& Pair, int32 HumanCommanders)
{
	FTargets Targets;
	if (TargetCount(HumanCommanders) == MaxTargets)
	{
		Targets.Region[0] = Pair.A;
		Targets.Region[1] = Pair.B;
		Targets.Count = 2;
		return Targets;
	}
	int32 Human[MaxRegions];
	Hops(World, World.EnemyHome, INDEX_NONE, Human);
	const bool bHoldA = Exists(World, Pair.A) && HumansHold(World, Pair.A);
	const bool bHoldB = Exists(World, Pair.B) && HumansHold(World, Pair.B);
	bool bB = false;
	if (bHoldA != bHoldB)
		bB = bHoldB;
	else if (ValidIndex(Pair.A) && ValidIndex(Pair.B))
		bB = Human[Pair.B] != INDEX_NONE && (Human[Pair.A] == INDEX_NONE || Human[Pair.B] < Human[Pair.A]);
	Targets.Region[0] = bB ? Pair.B : Pair.A;
	Targets.Count = 1;
	return Targets;
}

int32 UnitsFor(int32 Budget, int32 UnitCost)
{
	return UnitCost > 0 ? FMath::Clamp(Budget / UnitCost, 0, ForceUnits) : 0;
}

void AppendMemo(FStringBuilderBase& Out, int32 Ticket, int32 SizeBand, FStringView Region, float EtaSeconds)
{
	Out.Appendf(TEXT("Ticket #%d \u00B7 Attack: %s sends ~%d units to "), Ticket, Name, SizeBand);
	Out << Region << TEXT(" \u00B7 ETA ");
	JevIntent::AppendCountdown(Out, EtaSeconds);
}
}
