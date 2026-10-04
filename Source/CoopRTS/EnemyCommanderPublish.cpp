#include "EnemyCommanderTurn.h"

#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "MapRegion.h"

namespace
{
JevExecution::FPublished ViewOf(const FJevPublishedPlan& Plan)
{
	JevExecution::FPublished View;
	View.Ticket = Plan.TicketNumber;
	View.Verb = PlanVerb(Plan.Verb);
	View.Target = Plan.TargetRegionIndex;
	View.EtaSeconds = Plan.EtaSeconds;
	View.SizeBand = Plan.SizeBand;
	View.bEscalated = Plan.bEscalated;
	return View;
}

#if !UE_BUILD_SHIPPING
// Accepted transitions, retained for telemetry: every new ticket and every escalation.
void RecordHistory(const FJevTurn& Turn, const FJevForceStep& Step, const FJevPublishedPlan& Published,
	const JevPlanner::FPlan& Display)
{
	if (!Step.bNewCommitment && !Step.bEscalation)
		return;
	FJevPlanHistoryEntry& History = Turn.State->EnemyPlanHistory.AddDefaulted_GetRef();
	History.Plan = Published;
	History.TimeSeconds = Turn.Now;
	History.bEscalation = Step.bEscalation && !Step.bNewCommitment;
	History.ForceNumber = Step.Force->ForceNumber;
	AActor* Structure = Step.Force->TargetStructure;
	History.TargetStructureName = Structure ? Structure->GetName() : FString();
	History.SourceController = JevExecution::ValidRegion(Display.Source)
		? Turn.Summary.Regions[Display.Source].Controller
		: INDEX_NONE;
	History.bOrderChanged = Step.bChanged;
}
#endif
}

// Publish the executor's current order, including natural completion,
// without replacing the strategic ticket or restarting its commitment.
// A plan not issued yet (JEV's kit forces during planning) shows the plan the force will be given.
void AEnemyCommander::Publish(FJevTurn& Turn, const FJevForceStep& Step)
{
	if (Turn.Team != 5)
		return;
	AArmyGroup* Force = Step.Force;
	const JevPlanner::FPlan Display = Step.Next.bUnissued ? Step.Next
														  : JevExecution::DisplayPlan(Turn.Summary, Step.Snapshot, Step.Next, PlanVerb(Force->Verb), ActualTarget(*Force),
																Force->Verb == EForceVerb::MoveHold);
	TArray<FJevPublishedPlan>& Plans = Turn.State->EnemyPlans;
	FJevPublishedPlan* Existing = Plans.FindByPredicate([&](const FJevPublishedPlan& Entry) { return Entry.Force == Force; });
	const JevExecution::FPublished Before = Existing ? ViewOf(*Existing) : JevExecution::FPublished();
	const JevExecution::FPublicationChange Change = JevExecution::PublicationChange(
		Existing ? &Before : nullptr, Step.Current->TicketNumber, Display);
	FJevPublishedPlan& Published = Existing ? *Existing : Plans.AddDefaulted_GetRef();
	AActor* Structure = Force->TargetStructure;
	Published.TicketNumber = Step.Current->TicketNumber;
	Published.Force = Force;
	Published.ForceNumber = Force->ForceNumber;
	Published.Verb = OrderVerb(Display.Verb);
	Published.SourceRegionIndex = Display.Source;
	Published.TargetRegionIndex = Display.Target;
	Published.TargetStructure = Structure;
	Published.SizeBand = Display.SizeBand;
	Published.EtaSeconds = Display.EtaSeconds;
	if (Change.bEtaRestarted)
		Published.EtaIssuedAt = Turn.Now;
	Published.CommittedUntil = Step.Next.CommittedUntil;
	Published.RemainingCommitment = JevPlanner::Remaining(Step.Next, Turn.Now);
	Published.bEscalated = Display.bEscalated;
	if (Change.bMemoChanged)
		Published.Memo = MemoTemplates.Format(Display, Published.TicketNumber,
			Turn.Regions[Display.Target]->DisplayName.ToString());
#if !UE_BUILD_SHIPPING
	RecordHistory(Turn, Step, Published, Display);
#endif
}
