#include "JevIntentView.h"

#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "Engine/World.h"
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
		View.Memo = Plan.Memo;
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
