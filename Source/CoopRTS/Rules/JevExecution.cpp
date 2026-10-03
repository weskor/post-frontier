#include "JevExecution.h"

namespace JevExecution
{
int32 UnfilledRoleSlot(const int32 (&Counts)[RoleSlots])
{
	for (int32 Slot = 0; Slot < RoleSlots; ++Slot)
		if (Counts[Slot] == 0)
			return Slot;
	return Counts[0] <= Counts[1] ? 0 : 1;
}

bool HasAdvantage(int32 Friendly, int32 Enemy, int32 SquadSize, int32 FriendlyIncome, int32 EnemyIncome)
{
	return Friendly >= SquadSize && Friendly * 4 >= FMath::Max(1, Enemy) * 5 && FriendlyIncome >= EnemyIncome;
}

bool Recovering(float HealthFraction, bool bWasRecovering)
{
	return HealthFraction < RecoveryEnterHealth || (bWasRecovering && HealthFraction < RecoveryExitHealth);
}

bool IsThreatened(const JevPlanner::FWorld& World)
{
	for (const JevPlanner::FRegion& Region : World.Regions)
		if (Region.Controller == World.Team && (Region.Hostiles || Region.bAttacked))
			return true;
	return false;
}

float DepositScore(int32 RatePerSecond, double DistanceToHome)
{
	return RatePerSecond * DepositRateWeight - DistanceToHome / DepositDistanceScale;
}

bool CanAfford(int32 Resources, int32 Cost, int32 Reserve)
{
	return Resources >= Cost + Reserve;
}

bool HoldsClaim(const JevPlanner::FWorld& World, const JevPlanner::FPlan& Plan, float Now)
{
	return Now < Plan.CommittedUntil && Plan.Verb != JevPlanner::EVerb::Retreat && JevPlanner::TargetValid(World, Plan)
		&& World.Regions[Plan.Target].Controller != World.Team;
}

bool ClaimsTarget(const JevPlanner::FWorld& World, const JevPlanner::FPlan& Plan)
{
	return ValidRegion(Plan.Target) && Plan.Verb != JevPlanner::EVerb::Retreat
		&& World.Regions[Plan.Target].Controller != World.Team;
}

JevPlanner::FPlan ActualPlan(const JevPlanner::FWorld& World, const JevPlanner::FForce& Force,
	const JevPlanner::FPlan* Current, float Now, JevPlanner::EVerb ActualVerb, int32 ActualTarget,
	uint32 TargetIdentity)
{
	const bool bRetreat = ActualVerb == JevPlanner::EVerb::Retreat;
	JevPlanner::FPlan Actual;
	Actual.Verb = ActualVerb;
	Actual.Source = Force.Source;
	Actual.Target = ActualTarget;
	Actual.TargetIdentity = TargetIdentity;
	Actual.SizeBand = JevPlanner::SizeBand(Force.UnitCount);
	Actual.bRequiresUnownedTarget = !bRetreat && ValidRegion(ActualTarget)
		&& World.Regions[ActualTarget].Controller != World.Team;
	Actual.EtaSeconds = FMath::Max(0.f, JevPlanner::TravelSeconds(World, Force, ActualTarget))
		/ (bRetreat ? RetreatSpeedFactor : 1.f);
	Actual.CommittedUntil = Current && Now < Current->CommittedUntil
		? Current->CommittedUntil
		: Now + JevPlanner::CommitmentSeconds;
	return Actual;
}

JevPlanner::FPlan DisplayPlan(const JevPlanner::FWorld& World, const JevPlanner::FForce& Force,
	const JevPlanner::FPlan& Next, JevPlanner::EVerb ActualVerb, int32 ActualTarget, bool bActualHolding)
{
	JevPlanner::FPlan Display = Next;
	Display.Verb = ActualVerb;
	Display.Target = ActualTarget;
	if (Display.Verb != Next.Verb || Display.Target != Next.Target)
		Display.EtaSeconds = bActualHolding ? 0.f
											: FMath::Max(0.f, JevPlanner::TravelSeconds(World, Force, Display.Target))
				/ (ActualVerb == JevPlanner::EVerb::Retreat ? RetreatSpeedFactor : 1.f);
	Display.bEscalated = Next.bEscalated && bActualHolding && Display.Target == Next.Target;
	return Display;
}

void AdoptRetreatRegion(const JevPlanner::FWorld& World, const JevPlanner::FForce& Force, int32 RetreatRegion,
	JevPlanner::FPlan& Plan)
{
	if (Plan.Verb != JevPlanner::EVerb::Retreat || !ValidRegion(RetreatRegion))
		return;
	Plan.Target = RetreatRegion;
	Plan.EtaSeconds = JevPlanner::TravelSeconds(World, Force, RetreatRegion) / RetreatSpeedFactor;
}

bool IsEscalation(const JevPlanner::FPlan* Current, const JevPlanner::FPlan& Next)
{
	return Current && Next.bEscalated && (!Current->bEscalated || Current->Target != Next.Target);
}

bool NewCommitment(const JevPlanner::FPlan* Current, const JevPlanner::FPlan& Next)
{
	return !Current || Next.CommittedUntil != Current->CommittedUntil;
}

FOrderChange OrderChange(const JevPlanner::FPlan& Next, const JevPlanner::FPlan* Current, bool bActualDiffers)
{
	FOrderChange Change;
	Change.bFresh = NewCommitment(Current, Next);
	const bool bDecisionChanged = !Current || Next.Verb != Current->Verb || Next.Target != Current->Target
		|| Next.TargetIdentity != Current->TargetIdentity;
	Change.bChanged = bActualDiffers && (bDecisionChanged || Change.bFresh);
	return Change;
}

FPublicationChange PublicationChange(const FPublished* Existing, int32 Ticket, const JevPlanner::FPlan& Display)
{
	FPublicationChange Change;
	Change.bEtaRestarted = !Existing || Existing->Ticket != Ticket || Existing->Verb != Display.Verb
		|| Existing->Target != Display.Target || Existing->EtaSeconds != Display.EtaSeconds
		|| Existing->bEscalated != Display.bEscalated;
	Change.bMemoChanged = Change.bEtaRestarted || Existing->SizeBand != Display.SizeBand;
	return Change;
}

int32 ForwardRegion(const JevPlanner::FWorld& World, const FVector& EnemyHome, uint64 Contested, uint64 Producers)
{
	int32 Best = INDEX_NONE;
	float BestScore = -TNumericLimits<float>::Max();
	for (int32 Index = 0; Index < ForceOrders::MaxRegions; ++Index)
	{
		const JevPlanner::FRegion& Region = World.Regions[Index];
		const uint64 Bit = uint64(1) << Index;
		if (!Region.bExists || Region.bMain || Region.Controller != World.Team || (Contested & Bit) || (Producers & Bit))
			continue;
		const float Score = -FVector::Dist2D(Region.Position, EnemyHome);
		if (Score > BestScore)
		{
			BestScore = Score;
			Best = Index;
		}
	}
	return Best;
}

EEconomyAction NextEconomyAction(const FEconomy& Economy)
{
	if (!Economy.bThreatened && Economy.Established > 0 && Economy.Producers < MaxProducers && Economy.bForwardAnchor
		&& CanAfford(Economy.Resources, Economy.ProducerCost, Economy.Reserve))
		return EEconomyAction::BuildProducer;
	if (!Economy.bHasWorkshop && Economy.Established > 0 && Economy.bWorkshopDefined
		&& CanAfford(Economy.Resources, Economy.WorkshopCost, Economy.Reserve))
		return EEconomyAction::BuildWorkshop;
	if (Economy.bHasWorkshop && Economy.bWorkshopComplete && !Economy.bDoctrineChosen
		&& CanAfford(Economy.Resources, Economy.ResearchCost, Economy.Reserve))
		return EEconomyAction::Research;
	return EEconomyAction::None;
}
}
