#include "EnemyCommanderTurn.h"

#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Commands/OrderGraph.h"
#include "MapRegion.h"

int32 ActualTarget(const AArmyGroup& Force)
{
	return Force.Verb == EForceVerb::Retreat ? Force.GetRetreatRegion() : Force.TargetRegionIndex;
}

namespace
{
using JevExecution::ValidRegion;

AActor* StructureFor(const FJevTurn& Turn, uint32 Identity)
{
	AActor* Structure = nullptr;
	for (int32 Index = 0; Index < Turn.Targets.Num(); ++Index)
		if (Turn.Targets[Index].Identity == Identity)
			Structure = Turn.TargetActors[Index];
	return Structure;
}

void Unpublish(const FJevTurn& Turn, AArmyGroup* Force)
{
	if (Turn.Team == 5)
		Turn.State->EnemyPlans.RemoveAll([&](const FJevPublishedPlan& Entry) { return Entry.Force == Force; });
}

bool SendOrder(const FJevTurn& Turn, AArmyGroup* Force, const JevPlanner::FPlan& Plan, AActor* Structure)
{
	return FCommandService::IssueForceOrder(Turn.Commander, Force, OrderVerb(Plan.Verb),
		Plan.Verb == JevPlanner::EVerb::Retreat ? INDEX_NONE : Plan.Target, Structure)
		.IsAccepted();
}

bool ActualDiffers(const AArmyGroup& Force, const JevPlanner::FPlan& Next, AActor* Structure)
{
	return Force.Verb != OrderVerb(Next.Verb)
		|| (Next.Verb != JevPlanner::EVerb::Retreat && Force.TargetRegionIndex != Next.Target)
		|| Force.TargetStructure != Structure || Force.Orders.IsEmpty();
}

JevPlanner::FPlan ActualPlanOf(const FJevTurn& Turn, const FJevForceStep& Step)
{
	const AArmyGroup& Force = *Step.Force;
	return JevExecution::ActualPlan(Turn.Summary, Step.Snapshot, Step.Current ? &Step.Current->Plan : nullptr, Turn.Now,
		PlanVerb(Force.Verb), ActualTarget(Force), IsValid(Force.TargetStructure) ? Force.TargetStructure->GetUniqueID() : 0);
}

float JoinedHealthFraction(const AArmyGroup& Force)
{
	float Health = 0.f;
	int32 Joined = 0;
	for (const AArmyUnit* Unit : Force.GetUnits())
		if (IsValid(Unit) && Unit->IsAlive() && !Unit->IsReinforcing())
		{
			Health += float(Unit->GetHealth()) / Unit->MaxHealth();
			++Joined;
		}
	return Joined ? Health / Joined : 1.f;
}

void SnapshotForce(const FJevTurn& Turn, FJevForceStep& Step)
{
	const AArmyGroup& Force = *Step.Force;
	JevPlanner::FForce& Snapshot = Step.Snapshot;
	Snapshot.Source = ForceOrderGraph::SourceRegion(Force, *Turn.State);
	Snapshot.Home = Turn.HomeRegion->RegionIndex;
	Snapshot.Position = Force.GetCenter();
	Snapshot.UnitCount = Force.GetAliveCount();
	Snapshot.bRecovering = Step.Current && Step.Current->bRecovering;
	Snapshot.bRetreating = Force.Verb == EForceVerb::Retreat;
	Snapshot.HealthFraction = JoinedHealthFraction(Force);
	Snapshot.bCanRefill = !Step.bFree;
	Step.bRecovering = Snapshot.bCanRefill && JevExecution::Recovering(Snapshot.HealthFraction, Snapshot.bRecovering);
	Snapshot.bAtRecovery = Snapshot.bRecovering && Force.Verb == EForceVerb::MoveHold
		&& Force.IsHoldingRegion() && Force.HoldRegionIndex == Snapshot.Source;
	Step.Speed = Force.GetBaseMarchSpeed();
	Snapshot.ClassSpeeds = TConstArrayView<float>(&Step.Speed, 1);
}

// False when the force has neither a legal proposal nor an order to keep.
bool ChoosePlan(const FJevTurn& Turn, FJevForceStep& Step)
{
	const JevPlanner::FPlan* Prior = Step.Current ? &Step.Current->Plan : nullptr;
	Step.bCommandsRejected = Step.Current && Step.Current->bCommandsRejected && Turn.Now < Step.Current->Plan.CommittedUntil
		&& !JevPlanner::MustDefend(Turn.Summary, Step.Snapshot);
	if (Step.bCommandsRejected)
		Step.Next = ActualPlanOf(Turn, Step);
	else if (!JevPlanner::Decide(Turn.Summary, Step.Snapshot, Turn.Now, Prior, Step.Next))
	{
		if (Step.Force->Orders.IsEmpty())
		{
			Unpublish(Turn, Step.Force);
			return false;
		}
		Step.Next = ActualPlanOf(Turn, Step);
	}
	return true;
}

// A failed fresh choice must not monopolize every evaluation. Try the remaining
// legal proposals; a held defense cannot switch to an unrelated order.
bool TryAlternatives(const FJevTurn& Turn, FJevForceStep& Step)
{
	const JevPlanner::FCandidates Alternatives = JevPlanner::Propose(Turn.Summary, Step.Snapshot);
	for (int32 At = 0; At < Alternatives.Count; ++At)
	{
		JevPlanner::FPlan Alternative = Alternatives.Values[At].Plan;
		if (Alternative.Verb == Step.Next.Verb && Alternative.Target == Step.Next.Target
			&& Alternative.TargetIdentity == Step.Next.TargetIdentity)
			continue;
		if (!SendOrder(Turn, Step.Force, Alternative, StructureFor(Turn, Alternative.TargetIdentity)))
			continue;
		Alternative.CommittedUntil = Turn.Now + JevPlanner::CommitmentSeconds;
		Step.Next = Alternative;
		return true;
	}
	return false;
}

// False when the force's order was rejected, no alternative took and it has no order left.
bool RecoverRejected(const FJevTurn& Turn, FJevForceStep& Step)
{
	if (Step.bFresh && !Turn.bRush && TryAlternatives(Turn, Step))
		return true;
	if (Step.Force->Orders.IsEmpty())
	{
		Unpublish(Turn, Step.Force);
		return false;
	}
	Step.Next = ActualPlanOf(Turn, Step);
	Step.bChanged = false;
	Step.bCommandsRejected = true;
	return true;
}

bool IssueOrder(const FJevTurn& Turn, FJevForceStep& Step)
{
	AActor* Structure = StructureFor(Turn, Step.Next.TargetIdentity);
	const JevPlanner::FPlan* Prior = Step.Current ? &Step.Current->Plan : nullptr;
	const JevExecution::FOrderChange Change = JevExecution::OrderChange(
		Step.Next, Prior, ActualDiffers(*Step.Force, Step.Next, Structure));
	Step.bFresh = Change.bFresh;
	Step.bChanged = Change.bChanged;
	return !Step.bChanged || SendOrder(Turn, Step.Force, Step.Next, Structure) || RecoverRejected(Turn, Step);
}

// A retreating force's producer rallies at the retreat region.
void RallyRetreat(const FJevTurn& Turn, const FJevForceStep& Step)
{
	const AArmyGroup& Force = *Step.Force;
	if (Step.Next.Verb != JevPlanner::EVerb::Retreat || Force.Verb != EForceVerb::Retreat
		|| !ValidRegion(Force.GetRetreatRegion()))
		return;
	ACommandBuilding* Producer = Force.GetProductionBuilding();
	if (IsValid(Producer) && Producer->RallyRegionIndex != Force.GetRetreatRegion())
		FCommandService::SetRallyPoint(Turn.Commander, Producer, Force.GetRetreatRegion());
}

// The Attack plan a release gives a force, under a fresh commitment.
JevPlanner::FPlan WavePlan(const FJevTurn& Turn, const FJevForceStep& Step, int32 Target)
{
	JevPlanner::FPlan Plan;
	Plan.Verb = JevPlanner::EVerb::Attack;
	Plan.Source = Step.Snapshot.Source;
	Plan.Target = Target;
	Plan.SizeBand = JevPlanner::SizeBand(Step.Snapshot.UnitCount);
	Plan.EtaSeconds = FMath::Max(0.f, JevPlanner::TravelSeconds(Turn.Summary, Step.Snapshot, Target));
	Plan.CommittedUntil = Turn.Now + JevPlanner::CommitmentSeconds;
	Plan.bRequiresUnownedTarget = Turn.Summary.Regions[Target].Controller != Turn.Team;
	return Plan;
}

// The rush scenario's choice: Attack the opposing main whatever the planner would pick. Planner
// recovery, defence and expansion are off; the force's own casualty withdrawal and refill still run,
// and an Attack that already stands is not reissued (OrderChange), so a refill resumes it.
bool ChooseNextPlan(const FJevTurn& Turn, FJevForceStep& Step)
{
	if (!Turn.bRush || !ValidRegion(Turn.Summary.EnemyHome))
		return ChoosePlan(Turn, Step);
	Step.bRecovering = false;
	Step.Next = WavePlan(Turn, Step, Turn.Summary.EnemyHome);
	return true;
}

// A force already in the field joins a wave unless it is retreating, recovering or holding its attacked region.
bool JoinsWave(const FJevTurn& Turn, const FJevForceStep& Step)
{
	return !Step.bRecovering && !Step.Snapshot.bRetreating && !JevPlanner::MustDefend(Turn.Summary, Step.Snapshot);
}
}

void AEnemyCommander::ExecuteForces(FJevTurn& Turn)
{
	for (const FJevCommittedForce& Entry : CommittedForces)
		if (JevExecution::HoldsClaim(Turn.Summary, Entry.Plan, Turn.Now))
		{
			++Turn.Reservations[Entry.Plan.Target];
			Turn.Summary.Regions[Entry.Plan.Target].bClaimed = true;
		}
	for (AArmyGroup* Force : Turn.Forces)
		ExecuteForce(Turn, Force);
}

void AEnemyCommander::ExecuteForce(FJevTurn& Turn, AArmyGroup* Force)
{
	FJevForceStep Step;
	Step.Force = Force;
	Step.bFree = IsWaveForce(Force);
	Step.Current = CommittedForces.FindByPredicate([&](const FJevCommittedForce& Entry) { return Entry.Force == Force; });
	if (Step.Current && JevExecution::HoldsClaim(Turn.Summary, Step.Current->Plan, Turn.Now))
		Turn.Summary.Regions[Step.Current->Plan.Target].bClaimed = --Turn.Reservations[Step.Current->Plan.Target] > 0;
	SnapshotForce(Turn, Step);
	if (!ChooseNextPlan(Turn, Step) || !IssueOrder(Turn, Step))
		return;
	Commit(Turn, Step);
	Publish(Turn, Step);
}

void AEnemyCommander::ExecuteWaveForce(FJevTurn& Turn, AArmyGroup* Force, int32 Target, bool bJoining)
{
	FJevForceStep Step;
	Step.Force = Force;
	Step.bFree = IsWaveForce(Force);
	Step.Current = CommittedForces.FindByPredicate([&](const FJevCommittedForce& Entry) { return Entry.Force == Force; });
	SnapshotForce(Turn, Step);
	if ((bJoining && !JoinsWave(Turn, Step)) || !JevExecution::ValidRegion(Target))
		return;
	Step.Next = WavePlan(Turn, Step, Target);
	if (!IssueOrder(Turn, Step))
		return;
	Commit(Turn, Step);
	Publish(Turn, Step);
}

void AEnemyCommander::Commit(FJevTurn& Turn, FJevForceStep& Step)
{
	const JevPlanner::FPlan* Prior = Step.Current ? &Step.Current->Plan : nullptr;
	Step.bNewCommitment = JevExecution::NewCommitment(Prior, Step.Next);
	Step.bEscalation = JevExecution::IsEscalation(Prior, Step.Next);
	if (Step.bChanged)
		JevExecution::AdoptRetreatRegion(Turn.Summary, Step.Snapshot, Step.Force->GetRetreatRegion(), Step.Next);
	RallyRetreat(Turn, Step);
	if (!Step.Current)
	{
		Step.Current = &CommittedForces.AddDefaulted_GetRef();
		Step.Current->Force = Step.Force;
	}
	if (Step.bNewCommitment)
		Step.Current->TicketNumber = NextTicketNumber++;
	Step.Current->Plan = Step.Next;
	Step.Current->bRecovering = Step.bRecovering;
	Step.Current->bCommandsRejected = Step.bCommandsRejected;
	if (JevExecution::ClaimsTarget(Turn.Summary, Step.Next))
	{
		++Turn.Reservations[Step.Next.Target];
		Turn.Summary.Regions[Step.Next.Target].bClaimed = true;
	}
}
