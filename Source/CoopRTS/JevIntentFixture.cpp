#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "JevIntentFixture.h"

#include "ArmyTestSetup.h"
#include "JevIntentView.h"
#include "JevMemoTemplates.h"

namespace JevIntentFixture
{
namespace
{
constexpr float CommitmentSeconds = 25.f;
TWeakObjectPtr<AArmyGroup> Forces[2];
int32 NextTicket = 9001;

struct FRegions
{
	int32 Home = INDEX_NONE;
	int32 A = INDEX_NONE;
	int32 B = INDEX_NONE;
	int32 C = INDEX_NONE;
};

// JEV's main plus the three lowest-indexed regions that are not mains.
bool PickRegions(const ACommandGameState& State, FRegions& Out)
{
	TArray<int32, TInlineAllocator<8>> Open;
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region))
			continue;
		if (Region->RegionRole == ERegionRole::Main)
		{
			if (Region->HomeTeam == 5)
				Out.Home = Region->RegionIndex;
		}
		else
			Open.Add(Region->RegionIndex);
	}
	Open.Sort();
	if (Out.Home == INDEX_NONE || Open.Num() < 3)
		return false;
	Out.A = Open[0];
	Out.B = Open[1];
	Out.C = Open[2];
	return true;
}

FString EnsureForces(UWorld* World, ACommandGameState& State)
{
	for (int32 Index = 0; Index < 2; ++Index)
		if (!Forces[Index].IsValid())
		{
			const FVector Home = ArmyTestSetup::HostileStaging(&State) + FVector(0.f, Index * 300.f, 0.f);
			Forces[Index] = ArmyTestSetup::SpawnGroup(World, nullptr, -1, Home);
			if (!Forces[Index].IsValid())
				return TEXT("JEV fixture force did not spawn");
		}
	return FString();
}

FString Post(ACommandGameState& State, int32 Index, int32 Ticket, EForceVerb Verb, int32 Source, int32 Target,
	int32 SizeBand, float Eta, bool bEscalated)
{
	FJevMemoTemplates Templates;
	if (!Templates.Load())
		return TEXT("[JevMemos] templates did not load");
	const float Now = State.GetWorld()->GetTimeSeconds();
	JevPlanner::FPlan Plan;
	Plan.Verb = Verb == EForceVerb::Attack ? JevPlanner::EVerb::Attack
		: Verb == EForceVerb::Retreat      ? JevPlanner::EVerb::Retreat
										   : JevPlanner::EVerb::MoveAndHold;
	Plan.Source = Source;
	Plan.Target = Target;
	Plan.SizeBand = SizeBand;
	Plan.EtaSeconds = Eta;
	Plan.bEscalated = bEscalated;
	AArmyGroup* Force = Forces[Index].Get();
	FJevPublishedPlan* Existing = State.EnemyPlans.FindByPredicate([&](const FJevPublishedPlan& Entry) { return Entry.Force == Force; });
	FJevPublishedPlan& Published = Existing ? *Existing : State.EnemyPlans.AddDefaulted_GetRef();
	Published.TicketNumber = Ticket;
	Published.Force = Force;
	Published.ForceNumber = Force->ForceNumber > 0 ? Force->ForceNumber : Index + 1;
	Published.Verb = Verb;
	Published.SourceRegionIndex = Source;
	Published.TargetRegionIndex = Target;
	Published.TargetStructure = nullptr;
	Published.SizeBand = SizeBand;
	Published.EtaSeconds = Eta;
	Published.EtaIssuedAt = Now;
	Published.CommittedUntil = Now + CommitmentSeconds;
	Published.RemainingCommitment = CommitmentSeconds;
	Published.bEscalated = bEscalated;
	Published.Memo = Templates.Format(Plan, Ticket, JevIntentView::RegionName(State, Target));
	if (Published.Memo.IsEmpty())
		return TEXT("fixture plan did not format a memo");
	State.ForceNetUpdate();
	return FString();
}
}

FString Publish(UWorld* World, ACommandGameState& State, EStage Stage)
{
	if (!World || !State.HasAuthority())
		return TEXT("JEV plan fixture needs the authority world");
	FRegions Regions;
	if (!PickRegions(State, Regions))
		return TEXT("map has no JEV main and three open regions");
	for (TActorIterator<AEnemyCommander> It(World); It; ++It)
		It->SetActorTickEnabled(false);
	if (Stage == EStage::Create)
	{
		State.EnemyPlans.Reset();
		if (const FString Error = EnsureForces(World, State); !Error.IsEmpty())
			return Error;
		const FString First = Post(State, 0, NextTicket++, EForceVerb::Attack, Regions.Home, Regions.A, 8, 40.f, false);
		return First.IsEmpty() ? Post(State, 1, NextTicket++, EForceVerb::MoveHold, Regions.Home, Regions.B, 4, 25.f, false)
							   : First;
	}
	if (!Forces[0].IsValid() || !Forces[1].IsValid())
		return TEXT("create the fixture plans first");
	if (Stage == EStage::Escalate)
	{
		const FJevPublishedPlan* Held = State.EnemyPlans.FindByPredicate(
			[](const FJevPublishedPlan& Entry) { return Entry.Force == Forces[1].Get(); });
		if (!Held)
			return TEXT("fixture force 1 has no plan to escalate");
		return Post(State, 1, Held->TicketNumber, EForceVerb::MoveHold, Regions.B, Regions.B, Held->SizeBand, 0.f, true);
	}
	return Post(State, 0, NextTicket++, EForceVerb::Attack, Regions.Home, Regions.C, 6, 30.f, false);
}
}
#endif
