#include "JevIntentView.h"

#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "Engine/World.h"
#include "EnemyCommander.h"
#include "EngineUtils.h"
#include "MapRegion.h"

namespace JevIntentView
{
namespace
{
JevPlanner::EVerb PlannerVerb(EForceVerb Verb)
{
	return Verb == EForceVerb::Attack ? JevPlanner::EVerb::Attack
		: Verb == EForceVerb::Retreat ? JevPlanner::EVerb::Retreat
									  : JevPlanner::EVerb::MoveAndHold;
}
}

void Snapshot(const ACommandGameState& State, FPlans& Out)
{
	Out.Reset();
	const AEnemyCommander* Jev = nullptr;
	for (TActorIterator<AEnemyCommander> It(State.GetWorld()); It && !Jev; ++It)
		if (It->TeamIndex == 5)
			Jev = *It;
	for (const FJevPublishedPlan& Plan : State.EnemyPlans)
	{
		// The game state prunes plans whose force is gone; a stale entry is not a plan to show.
		if (!IsValid(Plan.Force))
			continue;
		JevIntent::FPlanView& View = Out.AddDefaulted_GetRef();
		View.Ticket = Plan.TicketNumber;
		View.Force = Plan.Force->GetUniqueID();
		View.ForceNumber = Plan.ForceNumber;
		View.Verb = PlannerVerb(Plan.Verb);
		View.Target = Plan.TargetRegionIndex;
		View.SizeBand = Plan.SizeBand;
		View.EtaSeconds = Plan.EtaSeconds;
		View.EtaIssuedAt = Plan.EtaIssuedAt;
		View.bEscalated = Plan.bEscalated;
		// A Split-Brain Cut force keeps the threat's name on its Attack plan for the whole march; one defending its region
		// shows the escalation like any other force.
		View.bCut = Jev && !Plan.bEscalated && Plan.Verb == EForceVerb::Attack && Jev->Release.CutForces.Contains(Plan.Force);
		View.Memo = Plan.Memo;
	}
	if (!Jev)
		return;
	// Split-Brain Cut plans, published ahead of their forces, read like any other plan; at launch the forces' own plans above
	// replace them. No force exists yet, so the plan's identity is its ticket, in a range actor ids never reach.
	for (const FJevCutPlan& Cut : Jev->Release.Cuts)
	{
		JevIntent::FPlanView& View = Out.AddDefaulted_GetRef();
		View.Ticket = Cut.Ticket;
		View.Force = 0x80000000u | static_cast<uint32>(Cut.Ticket);
		View.Verb = JevPlanner::EVerb::Attack;
		View.bCut = true;
		View.Target = Cut.Target;
		View.SizeBand = Cut.SizeBand;
		View.EtaSeconds = Cut.EtaSeconds;
		View.EtaIssuedAt = Cut.EtaIssuedAt;
		View.Memo = Cut.Memo;
	}
}

float Now(const ACommandGameState& State)
{
	return State.GetServerWorldTimeSeconds();
}

const FString& RegionName(const ACommandGameState& State, int32 Region)
{
	static const FString Unknown;
	for (const AMapRegion* Candidate : State.Regions)
		if (IsValid(Candidate) && Candidate->RegionIndex == Region)
			return Candidate->DisplayName.ToString();
	return Unknown;
}
}

bool UJevIntentFeed::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
		&& World->GetNetMode() != NM_DedicatedServer;
}

UJevIntentFeed* UJevIntentFeed::Get(const UObject* Context)
{
	const UWorld* World = Context ? Context->GetWorld() : nullptr;
	return World ? World->GetSubsystem<UJevIntentFeed>() : nullptr;
}

int32 UJevIntentFeed::Observe(const ACommandGameState& State)
{
	JevIntentView::FPlans Plans;
	JevIntentView::Snapshot(State, Plans);
	return Feed.Observe(Plans, JevIntentView::Now(State));
}
