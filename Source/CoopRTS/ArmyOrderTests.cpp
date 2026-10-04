#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyGroup.h"
#include "AIController.h"
#include "ArmyUnit.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "Commands/OrderGraph.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Navigation/PathFollowingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyReplacementTest, "CoopRTS.Orders.ReplaceHoldRetreat",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Runs against real characters, AI controllers and the loaded map's navmesh.
// Fresh standalone world; fixture homes derive from the placed HQs, not fixed armies.
class FArmyReplacementScenario : public IAutomationLatentCommand
{
public:
	explicit FArmyReplacementScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	virtual bool Update() override
	{
		IsolateWorld();
		if (FPlatformTime::Seconds() - Started > 35.)
		{
			if (RejectedInitialTarget.IsSet())
				Test->AddError(FString::Printf(TEXT("Timed out waiting for live army navigation: initial Move rejected at %s"),
					*RejectedInitialTarget.GetValue().ToString()));
			else
				Test->AddError(TEXT("Timed out waiting for live army navigation"));
			return true;
		}
		if (Stage == 0)
			return RunStage0();
		if (!Army.IsValid() || !Controller.IsValid())
		{
			Test->AddError(TEXT("Army or controller disappeared during orders"));
			return true;
		}
		const double Now = ArmyTestSetup::GameSeconds(Army->GetWorld());
		if (Stage == 1 && Now - StageStarted >= 1.5)
			return RunStage1(Now);
		if (Stage == 2 && HoldingSettled())
			return RunStage2(Now);
		if (Stage == 3)
			return RunStage3(Now);
		if (Stage == 4 && Now - StageStarted >= 1.)
			return RunStage4(Now);
		if (Stage == 5)
			return RunStage5(Now);
		if (Stage == 6)
			return RunStage6();
		return false;
	}
private:
	void IsolateWorld()
	{
		if (!bIsolated)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
				if (UWorld* World = Context.World())
					if (World->IsGameWorld() && World->GetNetMode() == NM_Standalone)
					{
						for (TActorIterator<AEnemyCommander> It(World); It; ++It)
							It->Destroy();
						bIsolated = true; // Keep order navigation independent of strategic AI.
						break;
					}
		}
	}
	bool RunStage0()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
		{
			UWorld* World = Context.World();
			if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client)
				continue;
			if (!ArmyTestSetup::CombatActors(World))
				continue;
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
			{
				if (auto* Owner = Cast<ACommandPlayerController>(It->GetOwner()))
				{
					if (Owner->IsLocalController() && It->GetArmyIndex() == 0 && It->GetUnits().Num() == 6)
					{
						Army = *It;
						Controller = Owner;
						break;
					}
				}
			}
		}
		if (!Army.IsValid() || ArmyTestSetup::GameSeconds(Army->GetWorld()) < 3. || !ArmyTestSetup::NavigationReady(Army->GetWorld()))
			return false;
		StartCenter = Army->GetCenter();
		Serial = Army->OrderSerial;
		const ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
		HomeRegion = ArmyTestSetup::CurrentRegion(Army.Get());
		AwayRegion = ArmyTestSetup::TravelRegion(Army.Get(), State->EnemyHeadquarters->GetActorLocation());
		const FVector InitialTarget = State->GetRegionAnchor(AwayRegion);
		FCommandService::IssueForceOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EForceVerb::MoveHold, AwayRegion);
		if (Army->OrderSerial == Serial)
		{
			RejectedInitialTarget = InitialTarget;
			return false; // Navmesh can still be generating.
		}
		RejectedInitialTarget.Reset();
		NextStage(ArmyTestSetup::GameSeconds(Army->GetWorld()));
		return false;
	}
	bool RunStage1(double Now)
	{
		Test->TestTrue(TEXT("Units actually move under the initial order"), FVector::Dist2D(StartCenter, Army->GetCenter()) > 100.);
		StartCenter = Army->GetCenter();
		const ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
		Serial = Army->OrderSerial;
		uint64 Graph[ForceOrders::MaxRegions];
		const int32 Count = ForceOrderGraph::ReadGraph(*State, Graph);
		const int32 Source = ForceOrderGraph::SourceRegion(*Army, *State);
		const AMapRegion* Current = ForceOrderGraph::Region(*State, Source);
		const int32 ReplacementWaypoint = Source == HomeRegion
				|| (Current && (!Current->Anchor || State->GetRegionController(Source) == Army->GetTeamIndex()))
			? ForceOrders::NextWaypoint(Graph, Count, Source, HomeRegion)
			: Source;
		FCommandService::IssueForceOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EForceVerb::MoveHold, HomeRegion);
		Test->TestTrue(TEXT("Replacement receives a new order serial"), Army->OrderSerial > Serial);
		Test->TestTrue(TEXT("Replacement records its new region and routes through the graph waypoint or assigned hold post"),
			Army->TargetRegionIndex == HomeRegion && Army->WaypointRegionIndex == ReplacementWaypoint
				&& FVector::Dist2D(Army->Destination, Army->IsHoldingRegion() ? Army->HoldPostLocation : State->GetRegionAnchor(ReplacementWaypoint)) < 100.f);
		NextStage(Now);
		return false;
	}
	bool RunStage2(double Now)
	{
		Test->TestTrue(TEXT("Units physically reach the replacement region's assigned post rather than stale intent"),
			Army->HoldRegionIndex == HomeRegion
				&& ArmyTestSetup::CurrentRegion(Army.Get()) == HomeRegion
				&& FVector::Dist2D(Army->GetCenter(), Army->HoldPostLocation) < 150.f);
		NextStage(Now);
		return false;
	}
	bool RunStage3(double Now)
	{
		if (!HoldingSettled())
		{
			StageStarted = Now;
			return false;
		}
		if (Now - StageStarted < .5)
			return false;
		HeldPositions.Reset();
		for (AArmyUnit* Unit : Army->GetUnits())
			HeldPositions.Add(Unit->GetActorLocation());
		Serial = Army->OrderSerial;
		FCommandService::IssueForceOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EForceVerb::MoveHold, ForceOrders::MaxRegions + 1);
		Test->TestEqual(TEXT("Invalid region preserves the accepted order"), Army->OrderSerial, Serial);
		NextStage(Now);
		return false;
	}
	bool RunStage4(double Now)
	{
		Test->TestTrue(TEXT("MoveHold persists at the settled replacement region"),
			Army->Verb == EForceVerb::MoveHold && Army->Status == EForceStatus::Holding
				&& Army->TargetRegionIndex == HomeRegion && HoldingSettled());
		for (int32 Index = 0; Index < Army->GetUnits().Num(); ++Index)
			Test->TestTrue(TEXT("Every unit stays stopped after Hold assembly settles"), FVector::Dist2D(Army->GetUnits()[Index]->GetActorLocation(), HeldPositions[Index]) < 5.);
		if (!Test->TestTrue(TEXT("Producerless fixture accepts real outward travel before Retreat"),
				!IsValid(Army->GetProductionBuilding())
					&& FCommandService::IssueForceOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EForceVerb::MoveHold, AwayRegion).IsAccepted()))
			return true;
		NextStage(Now);
		return false;
	}
	bool RunStage5(double Now)
	{
		const ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
		if (ArmyTestSetup::CurrentRegion(Army.Get()) == HomeRegion
			|| FVector::Dist2D(Army->GetCenter(), State->GetRegionAnchor(HomeRegion)) < 650.f)
			return false;
		RetreatStart = Army->GetCenter();
		if (!Test->TestTrue(TEXT("Retreat starts outside its remembered safe region and is accepted"),
				FCommandService::IssueForceOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EForceVerb::Retreat).IsAccepted()
					&& Army->Status == EForceStatus::Retreating && Army->WaypointRegionIndex == HomeRegion))
			return true;
		NextStage(Now);
		return false;
	}
	bool RunStage6()
	{
		bRetreatMoved |= FVector::Dist2D(Army->GetCenter(), RetreatStart) > 300.f;
		if (Army->Status != EForceStatus::Holding || !MembersStopped())
			return false;
		Test->TestTrue(TEXT("Producerless Retreat physically returns to the remembered safe region"),
			bRetreatMoved && Army->Verb == EForceVerb::MoveHold && Army->TargetRegionIndex == HomeRegion
				&& Army->HoldRegionIndex == INDEX_NONE && Army->HoldPostIndex == INDEX_NONE
				&& ArmyTestSetup::CurrentRegion(Army.Get()) == HomeRegion
				&& FVector::Dist2D(Army->GetCenter(), Army->Destination) < 35.f);
		for (const AArmyUnit* Unit : Army->GetUnits())
			Test->TestTrue(TEXT("Every retreating orphan member arrives at safety without an implicit post assignment"),
				FVector::Dist2D(Unit->GetActorLocation(), Army->Destination) < 450.f);
		Test->AddInfo(TEXT("Live navigation passed: region travel, replacement, physical Hold arrival, atomic invalid region and safe Retreat completion."));
		return true;
	}
	bool HoldingSettled() const
	{
		if (!Army->IsHoldingRegion() || Army->HoldPostIndex == INDEX_NONE || Army->bHoldResponding
			|| Army->HoldRegionIndex != Army->TargetRegionIndex
			|| FVector::Dist2D(Army->GetCenter(), Army->HoldPostLocation) >= 150.f)
			return false;
		return MembersStopped();
	}
	bool MembersStopped() const
	{
		for (const AArmyUnit* Unit : Army->GetUnits())
		{
			const AAIController* AI = Cast<AAIController>(Unit->GetController());
			if (!AI || AI->GetMoveStatus() != EPathFollowingStatus::Idle
				|| Unit->GetVelocity().Size2D() > 1.f)
				return false;
		}
		return true;
	}
	void NextStage(double Now)
	{
		++Stage;
		StageStarted = Now;
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<AArmyGroup> Army;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TArray<FVector> HeldPositions;
	FVector StartCenter = FVector::ZeroVector;
	FVector RetreatStart = FVector::ZeroVector;
	TOptional<FVector> RejectedInitialTarget;
	uint32 Serial = 0;
	int32 Stage = 0;
	int32 HomeRegion = INDEX_NONE;
	int32 AwayRegion = INDEX_NONE;
	bool bRetreatMoved = false;
	bool bIsolated = false;
	double Started;
	double StageStarted = 0;
};

bool FArmyReplacementTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyReplacementScenario(this));
	return true;
}

#endif
