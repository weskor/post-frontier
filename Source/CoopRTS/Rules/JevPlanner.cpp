#include "JevPlanner.h"

namespace JevPlanner
{
namespace
{
bool Exists(const FWorld& World, int32 Index)
{
	return Index >= 0 && Index < ForceOrders::MaxRegions && World.Regions[Index].bExists;
}

struct FPaths
{
	int32 Hops[ForceOrders::MaxRegions];
	float Length[ForceOrders::MaxRegions] = {};
};

FPaths Paths(const FWorld& World, const FForce& Force)
{
	FPaths Out;
	for (int32& Hop : Out.Hops)
		Hop = INDEX_NONE;
	if (!Exists(World, Force.Source))
		return Out;
	int32 Queue[ForceOrders::MaxRegions];
	int32 Read = 0, Write = 0;
	Queue[Write++] = Force.Source;
	Out.Hops[Force.Source] = 0;
	Out.Length[Force.Source] = FVector::Dist2D(Force.Position, World.Regions[Force.Source].Position);
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < ForceOrders::MaxRegions; ++Next)
			if (Exists(World, Next) && Out.Hops[Next] == INDEX_NONE && (World.Regions[Current].Neighbours & (uint64(1) << Next)))
			{
				Out.Hops[Next] = Out.Hops[Current] + 1;
				Out.Length[Next] = Out.Length[Current] + FVector::Dist2D(World.Regions[Current].Position, World.Regions[Next].Position);
				Queue[Write++] = Next;
			}
	}
	return Out;
}

float SlowestSpeed(const FForce& Force)
{
	float Speed = TNumericLimits<float>::Max();
	for (float ClassSpeed : Force.ClassSpeeds)
		if (ClassSpeed > 0.f)
			Speed = FMath::Min(Speed, ClassSpeed);
	return Speed == TNumericLimits<float>::Max() ? 0.f : Speed;
}

FPlan MakePlan(const FWorld& World, const FForce& Force, EVerb Verb, int32 Target, float Length)
{
	FPlan Plan;
	Plan.Verb = Verb;
	Plan.Source = Force.Source;
	Plan.Target = Target;
	Plan.SizeBand = SizeBand(Force.UnitCount);
	const float Speed = SlowestSpeed(Force) * (Verb == EVerb::Retreat ? 1.25f : 1.f);
	Plan.EtaSeconds = Speed > 0.f ? Length / Speed : 0.f;
	Plan.bRequiresUnownedTarget = Verb != EVerb::Retreat && World.Regions[Target].Controller != World.Team;
	return Plan;
}

bool Better(const FCandidate& A, const FCandidate& B)
{
	if (A.Score != B.Score)
		return A.Score > B.Score;
	if (A.Plan.Target != B.Plan.Target)
		return A.Plan.Target < B.Plan.Target;
	if (A.Plan.Verb != B.Plan.Verb)
		return uint8(A.Plan.Verb) < uint8(B.Plan.Verb);
	// Prefer a concrete hostile structure over a generic regional attack on a tie.
	if (!A.Plan.TargetIdentity || !B.Plan.TargetIdentity)
		return A.Plan.TargetIdentity != 0 && B.Plan.TargetIdentity == 0;
	return A.Plan.TargetIdentity < B.Plan.TargetIdentity;
}

void Offer(FCandidates& Out, const FPlan& Plan, float Score)
{
	const FCandidate Candidate{ Plan, Score };
	int32 At = 0;
	while (At < Out.Count && !Better(Candidate, Out.Values[At]))
		++At;
	if (At == MaxCandidates)
		return;
	Out.Count = FMath::Min(Out.Count + 1, MaxCandidates);
	for (int32 Index = Out.Count - 1; Index > At; --Index)
		Out.Values[Index] = Out.Values[Index - 1];
	Out.Values[At] = Candidate;
}

// The team's supply chain: which regions its main reaches through regions it controls.
struct FChain
{
	bool bActive = false;
	int32 Home = INDEX_NONE;
	uint64 Neighbours[ForceOrders::MaxRegions] = {};
	uint64 Controlled = 0;
	uint64 Connected = 0;

	uint64 Reach(uint64 ControlledMask) const
	{
		return ForceOrders::ConnectedMask(Neighbours, ForceOrders::MaxRegions, Home, ControlledMask);
	}
};

FChain MakeChain(const FWorld& World)
{
	FChain Chain;
	if (!Exists(World, World.Home))
		return Chain;
	Chain.bActive = true;
	Chain.Home = World.Home;
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
		if (Exists(World, Index))
		{
			Chain.Neighbours[Index] = World.Regions[Index].Neighbours;
			if (World.Regions[Index].Controller == World.Team)
				Chain.Controlled |= uint64(1) << Index;
		}
	Chain.Controlled |= uint64(1) << World.Home;
	Chain.Connected = Chain.Reach(Chain.Controlled);
	return Chain;
}

int32 IncomeIn(const FWorld& World, uint64 Mask)
{
	int32 Income = 0;
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
		if (Mask & (uint64(1) << Index))
			Income += World.Regions[Index].IncomeValue;
	return Income;
}

// What the supply chain adds to taking an unowned region, or to holding an owned one.
// Taking: an isolated deposit is worth nothing, and income the capture reconnects is worth
// ChainIncomeWeight per Power/s. Holding: the connected income that depends on the region.
float ChainScore(const FWorld& World, const FChain& Chain, int32 Index)
{
	if (!Chain.bActive)
		return 0.f;
	const uint64 Bit = uint64(1) << Index;
	if (Chain.Controlled & Bit)
		return ChainIncomeWeight * IncomeIn(World, Chain.Connected & ~Chain.Reach(Chain.Controlled & ~Bit));
	const uint64 After = Chain.Reach(Chain.Controlled | Bit);
	const float Isolated = (After & Bit) ? 0.f : -2.f * World.Regions[Index].DepositValue;
	return Isolated + ChainIncomeWeight * IncomeIn(World, After & ~Chain.Connected);
}

float RegionScore(const FWorld& World, const FForce& Force, const FChain& Chain, const FPaths& Route, int32 Index)
{
	const FRegion& Region = World.Regions[Index];
	return Index == World.EnemyHome ? (!World.bThreatened && World.bAdvantage ? 200.f : -50.f)
									: 8.f + Region.DepositValue * 2.f - Route.Hops[Index] * 5.f - Region.Hostiles * 4.f
			- FVector::DistSquared2D(Force.Position, Region.Position) / FMath::Square(4000.f)
			- (Region.Controller != INDEX_NONE ? 3.f : 0.f) + ChainScore(World, Chain, Index);
}

void OfferStructures(const FWorld& World, const FForce& Force, const FChain& Chain, const FPaths& Route, FCandidates& Out)
{
	for (const FTarget& Target : World.Targets)
	{
		if (!Target.Identity || !Target.bAlive || !Exists(World, Target.Region))
			continue;
		const FRegion& Region = World.Regions[Target.Region];
		if (Region.bClaimed || Region.Controller == World.Team || Route.Hops[Target.Region] == INDEX_NONE
			|| (Region.bMain && Target.Region != World.EnemyHome))
			continue;
		FPlan Plan = MakePlan(World, Force, EVerb::Attack, Target.Region, Route.Length[Target.Region]);
		Plan.TargetIdentity = Target.Identity;
		Offer(Out, Plan, RegionScore(World, Force, Chain, Route, Target.Region));
	}
}
}

bool MustDefend(const FWorld& World, const FForce& Force)
{
	return !Force.bRetreating && Exists(World, Force.Source)
		&& World.Regions[Force.Source].Controller == World.Team
		&& (World.Regions[Force.Source].Hostiles > 0 || World.Regions[Force.Source].bAttacked);
}

uint64 ConnectedRegions(const FWorld& World)
{
	return MakeChain(World).Connected;
}

int32 SizeBand(int32 UnitCount)
{
	return FMath::Max(2, FMath::RoundToInt(float(UnitCount) / 2.f) * 2);
}

float TravelSeconds(const FWorld& World, const FForce& Force, int32 Target)
{
	if (!Exists(World, Target))
		return -1.f;
	const FPaths Route = Paths(World, Force);
	const float Speed = SlowestSpeed(Force);
	return Route.Hops[Target] != INDEX_NONE && Speed > 0.f ? Route.Length[Target] / Speed : -1.f;
}

FCandidates Propose(const FWorld& World, const FForce& Force)
{
	FCandidates Out;
	if (!Exists(World, Force.Source) || Force.UnitCount <= 0 || SlowestSpeed(Force) <= 0.f)
		return Out;
	const FPaths Route = Paths(World, Force);
	const FChain Chain = MakeChain(World);
	if (Force.bRetreating)
	{
		// An in-flight Retreat remains Retreat even after its planning window
		// expires; the executor chooses safety if the home region is attacked.
		if (Exists(World, Force.Home) && Route.Hops[Force.Home] != INDEX_NONE)
			Offer(Out, MakePlan(World, Force, EVerb::Retreat, Force.Home, Route.Length[Force.Home]), 1000.f);
		return Out;
	}
	if (MustDefend(World, Force))
	{
		FPlan Defense = MakePlan(World, Force, EVerb::MoveAndHold, Force.Source, Route.Length[Force.Source]);
		Defense.bEscalated = true;
		Offer(Out, Defense, 100.f);
		return Out;
	}
	const bool bRecover = Force.bCanRefill && (Force.HealthFraction < .35f || (Force.bRecovering && Force.HealthFraction < .8f));
	if (bRecover && Force.bAtRecovery && Exists(World, Force.Source)
		&& World.Regions[Force.Source].Controller == World.Team && !World.Regions[Force.Source].Hostiles)
		Offer(Out, MakePlan(World, Force, EVerb::MoveAndHold, Force.Source, Route.Length[Force.Source]), 1001.f);
	if (Exists(World, Force.Home)
		&& World.Regions[Force.Home].Controller == World.Team && World.Regions[Force.Home].Hostiles == 0
		&& Route.Hops[Force.Home] != INDEX_NONE)
		Offer(Out, MakePlan(World, Force, EVerb::Retreat, Force.Home, Route.Length[Force.Home]), bRecover ? 1000.f : -1000.f);
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
	{
		const FRegion& Region = World.Regions[Index];
		if (!Region.bExists || Route.Hops[Index] == INDEX_NONE || Region.bClaimed)
			continue;
		if (Region.Controller == World.Team)
		{
			if (Region.Hostiles > 0 || Region.bAttacked || Index == Force.Source)
				Offer(Out, MakePlan(World, Force, EVerb::MoveAndHold, Index, Route.Length[Index]),
					Region.Hostiles > 0 || Region.bAttacked ? 100.f - Route.Hops[Index] * 5.f + ChainScore(World, Chain, Index)
															: -100.f);
			continue;
		}
		if (Region.bMain && Index != World.EnemyHome)
			continue;
		const EVerb Verb = Region.Controller == INDEX_NONE ? EVerb::MoveAndHold : EVerb::Attack;
		Offer(Out, MakePlan(World, Force, Verb, Index, Route.Length[Index]), RegionScore(World, Force, Chain, Route, Index));
	}
	OfferStructures(World, Force, Chain, Route, Out);
	return Out;
}

const FCandidate* Choose(const FCandidates& Candidates)
{
	const FCandidate* Best = nullptr;
	for (int32 Index = 0; Index < Candidates.Count; ++Index)
		if (!Best || Better(Candidates.Values[Index], *Best))
			Best = &Candidates.Values[Index];
	return Best;
}

bool TargetValid(const FWorld& World, const FPlan& Plan)
{
	if (!Exists(World, Plan.Target)
		|| (Plan.bRequiresUnownedTarget && World.Regions[Plan.Target].Controller == World.Team))
		return false;
	if (!Plan.TargetIdentity)
		return true;
	for (const FTarget& Target : World.Targets)
		if (Target.Identity == Plan.TargetIdentity && Target.Region == Plan.Target)
			return Target.bAlive;
	return false;
}

bool Decide(const FWorld& World, const FForce& Force, float Now, const FPlan* Current, FPlan& Out)
{
	if (Current && Now < Current->CommittedUntil && MustDefend(World, Force))
	{
		if (Current->bEscalated && Current->Verb == EVerb::MoveAndHold && Current->Target == Force.Source)
		{
			Out = *Current;
			return true;
		}
		Out = MakePlan(World, Force, EVerb::MoveAndHold, Force.Source,
			FVector::Dist2D(Force.Position, World.Regions[Force.Source].Position));
		Out.bEscalated = true;
		Out.bRequiresUnownedTarget = false;
		Out.CommittedUntil = Current->CommittedUntil;
		return true;
	}
	if (Current && Now < Current->CommittedUntil && TargetValid(World, *Current))
	{
		Out = *Current;
		return true;
	}
	const FCandidates Candidates = Propose(World, Force);
	const FCandidate* Best = Choose(Candidates);
	if (!Best)
		return false;
	Out = Best->Plan;
	Out.CommittedUntil = Now + CommitmentSeconds;
	return true;
}

float Remaining(const FPlan& Plan, float Now)
{
	return FMath::Max(0.f, Plan.CommittedUntil - Now);
}
}
