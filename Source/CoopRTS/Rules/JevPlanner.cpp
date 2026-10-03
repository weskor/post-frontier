#include "JevPlanner.h"

namespace JevPlanner
{
namespace
{
bool Exists(const FWorld& World, int32 Index)
{
	return Index >= 0 && Index < ForceGoals::MaxRegions && World.Regions[Index].bExists;
}

struct FPaths
{
	int32 Hops[ForceGoals::MaxRegions];
	float Length[ForceGoals::MaxRegions] = {};
};

FPaths Paths(const FWorld& World, const FForce& Force)
{
	FPaths Out;
	for (int32& Hop : Out.Hops)
		Hop = INDEX_NONE;
	if (!Exists(World, Force.Source))
		return Out;
	int32 Queue[ForceGoals::MaxRegions];
	int32 Read = 0, Write = 0;
	Queue[Write++] = Force.Source;
	Out.Hops[Force.Source] = 0;
	Out.Length[Force.Source] = FVector::Dist2D(Force.Position, World.Regions[Force.Source].Position);
	while (Read < Write)
	{
		const int32 Current = Queue[Read++];
		for (int32 Next = 0; Next < ForceGoals::MaxRegions; ++Next)
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
	return uint8(A.Plan.Verb) < uint8(B.Plan.Verb);
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
	const bool bRecover = Force.HealthFraction < .35f || (Force.bRecovering && Force.HealthFraction < .8f);
	if (Exists(World, Force.Home) && World.Regions[Force.Home].bTargetAlive
		&& World.Regions[Force.Home].Controller == World.Team && World.Regions[Force.Home].Hostiles == 0
		&& Route.Hops[Force.Home] != INDEX_NONE)
		Offer(Out, MakePlan(World, Force, EVerb::Retreat, Force.Home, Route.Length[Force.Home]), bRecover ? 1000.f : -1000.f);
	for (int32 Index = 0; Index < ForceGoals::MaxRegions; ++Index)
	{
		const FRegion& Region = World.Regions[Index];
		if (!Region.bExists || !Region.bTargetAlive || Route.Hops[Index] == INDEX_NONE || Region.bClaimed)
			continue;
		if (Region.Controller == World.Team)
		{
			if (Region.Hostiles > 0 || Region.bAttacked || Index == Force.Source)
				Offer(Out, MakePlan(World, Force, EVerb::MoveAndHold, Index, Route.Length[Index]),
					Region.Hostiles > 0 || Region.bAttacked ? 100.f - Route.Hops[Index] * 5.f : -100.f);
			continue;
		}
		if (Region.bMain && Index != World.EnemyHome)
			continue;
		const EVerb Verb = Region.Controller == INDEX_NONE ? EVerb::MoveAndHold : EVerb::Attack;
		const float Score = Index == World.EnemyHome ? (!World.bThreatened && World.bAdvantage ? 200.f : -50.f)
													 : 8.f + Region.DepositValue * 2.f - Route.Hops[Index] * 5.f - Region.Hostiles * 4.f
				- FVector::DistSquared2D(Force.Position, Region.Position) / FMath::Square(4000.f)
				- (Region.Controller != INDEX_NONE ? 3.f : 0.f);
		Offer(Out, MakePlan(World, Force, Verb, Index, Route.Length[Index]), Score);
	}
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
	return Exists(World, Plan.Target) && World.Regions[Plan.Target].bTargetAlive
		&& (!Plan.bRequiresUnownedTarget || World.Regions[Plan.Target].Controller != World.Team);
}

bool Decide(const FWorld& World, const FForce& Force, float Now, const FPlan* Current, FPlan& Out)
{
	if (Current && Now < Current->CommittedUntil && Exists(World, Force.Source)
		&& (World.Regions[Force.Source].Hostiles > 0 || World.Regions[Force.Source].bAttacked))
	{
		if (Current->bEscalated && Current->Target == Force.Source)
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
