#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "HoldAlarmFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHoldBuildingEdgeTest, "CoopRTS.Hold.BuildingEdge",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace HoldAlarmBuildingEdgeTests
{
class FScenario : public HoldAlarmFixture::FScenario
{
public:
	FScenario(FAutomationTestBase* InTest)
		: HoldAlarmFixture::FScenario(InTest, HoldAlarmFixture::ECase::BuildingEdge) {}

	virtual bool Update() override
	{
		double Now = 0.;
		const EStep Preparation = PrepareUpdate(Now);
		if (Preparation != EStep::Continue)
			return Preparation == EStep::Done;
		switch (Stage)
		{
		case EStage::Posts:
			return RunPosts(Now);
		case EStage::Respond:
			return RunRespond(Now);
		case EStage::Combat:
			return RunCombat(Now);
		case EStage::Return:
			return RunReturn(Now);
		default:
			return bFailed;
		}
	}

private:
	virtual bool CheckPostsForCase() override
	{
		if (!Check(Region->Contains(Holders[0]->GetCenter())
					&& FVector::Dist2D(Holders[0]->GetCenter(), OutsideEntryStart) > 500.,
				TEXT("A holder starting outside the region physically enters and reaches its assigned post"))
			|| !CheckRegionlessMoveHold())
			return false;
		return true;
	}

	virtual bool AdvanceResponse(double Now) override
	{
		for (const TWeakObjectPtr<AArmyGroup>& Holder : Holders)
			if (!Check(Holder->GetUnits()[0]->GetUnitRole() == EUnitRole::Frontline
						&& FVector::Dist2D(Holder->GetCenter(), Threats[0]->GetActorLocation())
							> Holder->GetUnits()[0]->WeaponRange() + 100.,
					TEXT("Live Frontline responders begin their approach outside melee weapon range")))
				return true;
		return HoldAlarmFixture::FScenario::AdvanceResponse(Now);
	}

	bool CheckRegionlessMoveHold()
	{
		AArmyGroup& Holder = *Holders[0];
		const EArmyOrder AcceptedOrder = Holder.Order;
		const FVector AcceptedDestination = Holder.Destination;
		const uint32 AcceptedSerial = Holder.OrderSerial;
		const EForceVerb AcceptedVerb = Holder.Verb;
		const EForceStatus AcceptedStatus = Holder.Status;
		const int32 AcceptedTarget = Holder.TargetRegionIndex, AcceptedWaypoint = Holder.WaypointRegionIndex;
		const AActor* AcceptedStructure = Holder.TargetStructure;
		const TArray<FForceOrder> AcceptedOrders = Holder.Orders;
		const float AcceptedMarchSpeed = Holder.MarchSpeed;
		const AArmyUnit* Member = Holder.GetUnits()[0];
		const FVector AcceptedPosition = Member->GetActorLocation(), AcceptedVelocity = Member->GetVelocity();
		const FVector AcceptedPursuitGoal = Member->PursuitGoal;
		const bool bAcceptedPursuing = Member->bPursuing;
		const AAIController* AI = Cast<AAIController>(Member->GetController());
		if (!Check(AI != nullptr, TEXT("The accepted holder retains its real movement controller")))
			return false;
		const EPathFollowingStatus::Type AcceptedMoveStatus = AI->GetMoveStatus();
		const int32 AcceptedRegion = Holder.HoldRegionIndex, AcceptedPost = Holder.HoldPostIndex;
		const FVector AcceptedPostLocation = Holder.HoldPostLocation;
		const bool bAcceptedResponding = Holder.bHoldResponding;
		const AArmyUnit* AcceptedThreat = Holder.HoldThreat;
		const AActor* AcceptedAsset = Holder.HoldThreatenedAsset;
		const EHoldThreatKind AcceptedKind = Holder.HoldThreatKind;
		const double AcceptedStarted = Holder.GetHoldResponseStarted(), AcceptedQuiet = Holder.GetHoldQuietSince();
		const FVector Ground = State->GetRegionAnchor(Region->RegionIndex);
		// Omit the actual target region without allowing a world tick between
		// the rejected command and restoration of the live map.
		const int32 RegionSlot = State->Regions.IndexOfByKey(Region.Get());
		if (!Check(RegionSlot != INDEX_NONE && State->FindRegionAt(Ground) == Region.Get()
					&& AcceptedVerb == EForceVerb::MoveHold && AcceptedStatus == EForceStatus::Holding
					&& AcceptedTarget == Region->RegionIndex && !AcceptedOrders.IsEmpty(),
				TEXT("The rejection fixture begins with an accepted active regional MoveHold")))
			return false;
		if (!CheckRejectedRegionlessOrder(Holder, Ground, RegionSlot, AcceptedOrders))
			return false;
		return Check(Holder.Order == AcceptedOrder && Holder.Destination.Equals(AcceptedDestination, 1.)
				&& Holder.OrderSerial == AcceptedSerial && Holder.Verb == AcceptedVerb && Holder.Status == AcceptedStatus
				&& Holder.TargetRegionIndex == AcceptedTarget && Holder.TargetStructure == AcceptedStructure
				&& Holder.WaypointRegionIndex == AcceptedWaypoint && Holder.MarchSpeed == AcceptedMarchSpeed
				&& Member->GetActorLocation() == AcceptedPosition && Member->GetVelocity() == AcceptedVelocity
				&& Member->PursuitGoal == AcceptedPursuitGoal && Member->bPursuing == bAcceptedPursuing
				&& AI->GetMoveStatus() == AcceptedMoveStatus
				&& Holder.HoldRegionIndex == AcceptedRegion && Holder.HoldPostIndex == AcceptedPost
				&& Holder.HoldPostLocation.Equals(AcceptedPostLocation, 1.) && Holder.bHoldResponding == bAcceptedResponding
				&& Holder.HoldThreat == AcceptedThreat && Holder.HoldThreatenedAsset == AcceptedAsset && Holder.HoldThreatKind == AcceptedKind
				&& Holder.GetHoldResponseStarted() == AcceptedStarted && Holder.GetHoldQuietSince() == AcceptedQuiet,
			TEXT("Rejected regionless MoveHold preserves accepted verb, status, target, movement and complete Hold state"));
	}

	bool CheckRejectedRegionlessOrder(AArmyGroup& Holder, const FVector& Ground, int32 RegionSlot,
		const TArray<FForceOrder>& AcceptedOrders)
	{
		State->Regions.RemoveAt(RegionSlot);
		const bool bRegionless = State->FindRegionAt(Ground) == nullptr;
		const FCommandResult Result = FCommandService::IssueForceOrder(Holder.GetOwningPlayerState(), &Holder,
			EForceVerb::MoveHold, Region->RegionIndex);
		State->Regions.Insert(Region.Get(), RegionSlot);
		if (!Check(bRegionless && !Result.IsAccepted(), TEXT("MoveHold rejects its missing target region"))
			|| !Check(Holder.Orders.Num() == AcceptedOrders.Num(), TEXT("Rejected MoveHold preserves the accepted order queue")))
			return false;
		for (int32 Index = 0; Index < AcceptedOrders.Num(); ++Index)
		{
			const FForceOrder& Before = AcceptedOrders[Index];
			const FForceOrder& After = Holder.Orders[Index];
			if (!Check(After.Verb == Before.Verb && After.RegionIndex == Before.RegionIndex
						&& After.Structure == Before.Structure && After.SelectionSpeed == Before.SelectionSpeed
						&& After.bStructureTarget == Before.bStructureTarget,
					TEXT("Rejected MoveHold preserves every accepted active and queued order")))
				return false;
		}
		return true;
	}
};
}

bool FHoldBuildingEdgeTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(HoldAlarmBuildingEdgeTests::FScenario(this));
	return true;
}

#endif
