#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "AIController.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "CommandPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationData.h"
#include "NavigationSystem.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyTwoGroupsTest, "CoopRTS.Movement.TwoGroups",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Run alone in a fresh Boot standalone game. All observations are of real AI paths
// and character locations; every arrival includes every member, not just the center.
class FArmyTwoGroupsScenario : public IAutomationLatentCommand
{
public:
	explicit FArmyTwoGroupsScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (!bIsolated)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
				if (UWorld* World = Context.World())
					if (World->IsGameWorld() && World->GetNetMode() == NM_Standalone)
					{
						for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
						bIsolated = true; // Crossing retains its original static defender.
						break;
					}
		}
		const double Now = FPlatformTime::Seconds();
		if (bFailed) return true;
		if (Now - Started > 90.)
		{
			Test->AddError(FString::Printf(TEXT("TwoGroups exceeded 90 seconds in stage %d"), static_cast<int32>(Stage)));
			for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
			{
				if (!Groups[GroupIndex].IsValid()) continue;
				for (int32 Index = 0; Index < Groups[GroupIndex]->Units.Num(); ++Index)
				{
					const AArmyUnit* Unit = Groups[GroupIndex]->Units[Index];
					if (!IsValid(Unit)) continue;
					const UPathFollowingComponent* Following = GetFollowing(Unit);
					Test->AddInfo(FString::Printf(TEXT("Army %d unit %d position=%s goal=%s speed=%.1f status=%d reached=%d"),
						GroupIndex, Index, *Unit->GetActorLocation().ToCompactString(),
						Goals[GroupIndex].IsValidIndex(Index) ? *Goals[GroupIndex][Index].ToCompactString() : TEXT("unset"),
						Unit->GetVelocity().Size2D(), Following ? static_cast<int32>(Following->GetStatus()) : -1,
						Following && Following->DidMoveReachGoal()));
				}
			}
			return true;
		}

		if (Stage == EStage::FindGroups) return Start(Now);
		if (!Check(Controller.IsValid() && Groups[0].IsValid() && Groups[1].IsValid() && Enemy.IsValid(),
			TEXT("Both groups, their controller and the opposing army survive"))) return true;
		for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
		{
			if (!Check(Groups[GroupIndex]->Units.Num() == 6, TEXT("No units disappear or appear unexpectedly"))) return true;
			for (AArmyUnit* Unit : Groups[GroupIndex]->Units)
			{
				if (!Check(IsValid(Unit) && Unit->Group == Groups[GroupIndex].Get(), TEXT("Every unit retains its own group"))) return true;
				if (Stage >= EStage::Crossing)
				{
					const FVector Location = Unit->GetActorLocation();
					// CentralObstacle occupies X +/-600, Y +/-1000. Crossing this
					// strip outside its footprint proves each character went around it.
					if (FMath::Abs(Location.X) < 550. && FMath::Abs(Location.Y) > 1000.)
						WentAroundObstacle.Add(Unit);
				}
			}
		}

		switch (Stage)
		{
		case EStage::Exchange:
			if (AllArrived())
			{
				Test->AddInfo(TEXT("Both six-unit groups exchanged home positions without stragglers."));
				SetStage(EStage::RapidReplacement, Now);
			}
			break;
		case EStage::RapidReplacement:
			if (Now - StageStarted >= .12)
			{
				const double Offset = (ReplacementCount % 2 == 0) ? 250. : -250.;
				if (!Move(0, FVector(-2800. + Offset, 1900., 0.)) || !Move(1, FVector(-1800. - Offset, -1900., 0.))) return true;
				StageStarted = Now;
				if (++ReplacementCount == 8) SetStage(EStage::BeforeRejection, Now);
			}
			break;
		case EStage::BeforeRejection:
			if (Now - StageStarted >= .6)
			{
				UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(Controller->GetWorld());
				FNavLocation Projected;
				const FNavAgentProperties& Agent = Groups[0]->Units[0]->GetNavAgentPropertiesRef();
				if (!Check(Navigation && Navigation->ProjectPointToNavigation(FVector(0., 4370., 0.), Projected, FVector(75., 75., 200.), &Agent),
					TEXT("Near-wall rejection probe has a navigable center"))) return true;
				for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
				{
					for (AArmyUnit* Unit : Groups[GroupIndex]->Units)
						if (!Check(Unit->GetVelocity().Size2D() > 25., TEXT("Every unit is moving before rejected requests"))) return true;
					// A click on the obstacle's top is not a reachable ground order;
					// north-wall center is on the floor but its +180 Y slots cannot fit.
					if (!RejectMove(GroupIndex, FVector(100000., 0., 0.), TEXT("Out-of-bounds"))
						|| !RejectMove(GroupIndex, FVector(0., 0., 600.), TEXT("Unreachable obstacle top"))
						|| !RejectMove(GroupIndex, FVector(0., 4370., 0.), TEXT("Near-wall formation"))) return true;
					RememberPositions(GroupIndex);
				}
				SetStage(EStage::AfterRejection, Now);
			}
			break;
		case EStage::AfterRejection:
			if (Now - StageStarted >= .8)
			{
				for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
					for (int32 Index = 0; Index < Groups[GroupIndex]->Units.Num(); ++Index)
					{
						const FVector Position = Groups[GroupIndex]->Units[Index]->GetActorLocation();
						if (!Check(FVector::Dist2D(Position, Positions[GroupIndex][Index]) > 30., TEXT("Rejected requests do not stop any unit"))
							|| !Check(FVector::Dist2D(Position, Goals[GroupIndex][Index]) + 30. < FVector::Dist2D(Positions[GroupIndex][Index], Goals[GroupIndex][Index]),
								TEXT("Every unit continues toward its accepted replacement"))) return true;
					}
				if (!Move(0, FVector(2200., 400., 0.)) || !Move(1, FVector(2200., -400., 0.))) return true;
				SetStage(EStage::Crossing, Now);
			}
			break;
		case EStage::Crossing:
			if (Now - StageStarted >= 2.)
			{
				if (!Move(0, FVector(2800., 650., 0.))) return true;
				for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
				{
					const uint32 Serial = Groups[GroupIndex]->OrderSerial;
					Controller->ServerIssueOrder(Groups[GroupIndex].Get(), EArmyOrder::Hold, FVector::ZeroVector);
					if (!Check(Groups[GroupIndex]->Order == EArmyOrder::Hold && Groups[GroupIndex]->OrderSerial > Serial,
						TEXT("Immediate Hold replaces travel for every member"))) return true;
					RememberPositions(GroupIndex);
				}
				SetStage(EStage::Held, Now);
			}
			break;
		case EStage::Held:
			for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
				for (int32 Index = 0; Index < Groups[GroupIndex]->Units.Num(); ++Index)
				{
					AArmyUnit* Unit = Groups[GroupIndex]->Units[Index];
					UPathFollowingComponent* Following = GetFollowing(Unit);
					if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Positions[GroupIndex][Index]) < 5.
						&& Unit->GetVelocity().Size2D() < 1. && Following && Following->GetStatus() == EPathFollowingStatus::Idle,
						FString::Printf(TEXT("Army %d unit %d stays at its immediate Hold position"), GroupIndex, Index))) return true;
				}
			if (Now - StageStarted >= 1.)
			{
				if (!Move(0, FVector(2300., 400., 0.)) || !Move(1, FVector(2300., -400., 0.))) return true;
				SetStage(EStage::FinalArrival, Now);
			}
			break;
		case EStage::FinalArrival:
			if (AllArrived()) SetStage(EStage::Settled, Now);
			break;
		case EStage::Settled:
			if (!Check(AllArrived(), TEXT("No stale order resumes after all units reach the latest destination"))) return true;
			if (Now - StageStarted >= 1.)
			{
				for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
					for (AArmyUnit* Unit : Group->Units)
						if (!Check(WentAroundObstacle.Contains(Unit), FString::Printf(TEXT("%s crossed around the central obstacle"), *Unit->GetName()))) return true;
				Test->AddInfo(TEXT("TwoGroups passed: independent armies, every-unit exchange and obstacle crossing, eight rapid replacements, atomic invalid orders with continued motion, immediate per-unit Hold, and all twelve units at their latest goals."));
				return true;
			}
			break;
		default: break;
		}
		return bFailed;
	}

private:
	enum class EStage : uint8 { FindGroups, Exchange, RapidReplacement, BeforeRejection, AfterRejection, Crossing, Held, FinalArrival, Settled };

	bool Check(bool bCondition, const FString& Message)
	{
		if (!bCondition) { Test->AddError(Message); bFailed = true; }
		return bCondition;
	}

	static UPathFollowingComponent* GetFollowing(const AArmyUnit* Unit)
	{
		const AAIController* AI = Unit ? Cast<AAIController>(Unit->GetController()) : nullptr;
		return AI ? AI->GetPathFollowingComponent() : nullptr;
	}

	bool bEnemyPositioned = false;
	bool Start(double Now)
	{
		if (Now - Started < 3.) return false;
		if (!Controller.IsValid())
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || !World->IsGameWorld() || World->GetNetMode() != NM_Standalone) continue;
				for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
					if (It->IsLocalController()) { Controller = *It; break; }
				if (Controller.IsValid()) break;
			}
		}
		if (!Controller.IsValid()) return false;
		if (!ArmyTestSetup::CombatActors(Controller->GetWorld())) return false;
		for (TActorIterator<AArmyGroup> It(Controller->GetWorld()); It; ++It)
		{
			if (It->bOpposingArmy) Enemy = *It;
			if (It->GetOwner() == Controller.Get() && It->ArmyIndex >= 0 && It->ArmyIndex < 2)
			{
				if (!Check(!Groups[It->ArmyIndex].IsValid() || Groups[It->ArmyIndex].Get() == *It, TEXT("ArmyIndex uniquely identifies each local group"))) return true;
				Groups[It->ArmyIndex] = *It;
			}
		}
		if (!Groups[0].IsValid() || !Groups[1].IsValid() || !Enemy.IsValid() || Enemy->Units.IsEmpty()) return false;
		if (!bEnemyPositioned)
		{
			// The strategic actor may have travelled before automation attaches.
			// Restore this historical movement-only encounter's static defender.
			for (AArmyUnit* Unit : Enemy->Units)
			{
				const int32 Slot = Unit->CompositionSlot;
				FVector Position = Unit->GetActorLocation();
				Position.X = Enemy->HomeLocation.X - (1 - Slot / 2) * 220.f;
				Position.Y = Enemy->HomeLocation.Y + (Slot % 2 ? 1.f : -1.f) * 140.f;
				Unit->SetActorLocation(Position, false, nullptr, ETeleportType::TeleportPhysics);
			}
			Enemy->IssueHold();
			bEnemyPositioned = true;
		}
		TSet<AArmyUnit*> UniqueUnits;
		for (const TWeakObjectPtr<AArmyGroup>& Group : Groups)
		{
			if (!Check(Group->OrderSerial == 0 && Group->Order == EArmyOrder::Hold && Group->Units.Num() == 6,
				TEXT("TwoGroups requires fresh Boot groups: six units and no earlier orders"))) return true;
			for (AArmyUnit* Unit : Group->Units)
			{
				if (!Check(IsValid(Unit) && Unit->Group == Group.Get()
					&& Unit->TeamIndex == Group->TeamIndex && !UniqueUnits.Contains(Unit)
					&& FVector::Dist2D(Unit->GetActorLocation(), Group->HomeLocation) < 450., TEXT("Each starting member belongs exclusively to its group at home"))) return true;
				UniqueUnits.Add(Unit);
			}
		}
		if (!Check(Groups[0]->TeamIndex == Groups[1]->TeamIndex && Groups[0]->GetOwner() == Groups[1]->GetOwner(), TEXT("Two independent armies belong to the same player"))) return true;
		// A rejected first request is safe to retry while dynamic navmesh starts.
		Controller->ServerIssueOrder(Groups[0].Get(), EArmyOrder::Move, Groups[0]->HomeLocation + FVector(0., 650., 0.));
		if (Groups[0]->OrderSerial == 0) return false;
		if (!Check(Groups[1]->OrderSerial == 0 && Groups[1]->Order == EArmyOrder::Hold,
			TEXT("Ordering army zero does not order army one"))) return true;
		if (!Move(0, Groups[1]->HomeLocation) || !Move(1, Groups[0]->HomeLocation)) return true;
		SetStage(EStage::Exchange, Now);
		return false;
	}

	bool Move(int32 GroupIndex, const FVector& Target)
	{
		AArmyGroup* Group = Groups[GroupIndex].Get();
		const AArmyGroup* Other = Groups[1 - GroupIndex].Get();
		const uint32 Serial = Group->OrderSerial;
		const uint32 OtherSerial = Other->OrderSerial;
		const EArmyOrder OtherOrder = Other->Order;
		const FVector OtherDestination = Other->Destination;
		Controller->ServerIssueOrder(Group, EArmyOrder::Move, Target);
		return Check(Group->OrderSerial > Serial && Group->Order == EArmyOrder::Move
			&& FVector::Dist2D(Group->Destination, Target) < 100., TEXT("Valid replacement is accepted at the requested destination"))
			&& Check(Other->OrderSerial == OtherSerial && Other->Order == OtherOrder && Other->Destination.Equals(OtherDestination), TEXT("Ordering one army preserves the other army's accepted order"))
			&& CaptureGoals(GroupIndex);
	}

	bool CaptureGoals(int32 GroupIndex)
	{
		Goals[GroupIndex].Reset();
		for (AArmyUnit* Unit : Groups[GroupIndex]->Units)
		{
			UPathFollowingComponent* Following = GetFollowing(Unit);
			if (!Check(Following && Following->GetPath().IsValid() && Following->GetStatus() == EPathFollowingStatus::Moving,
				TEXT("Every member receives a real active navigation path"))) return false;
			const FVector Goal = Following->GetPath()->GetEndLocation();
			if (!Check(FVector::Dist2D(Goal, Groups[GroupIndex]->Destination) < 450., TEXT("Every member's path ends in the commanded formation"))) return false;
			Goals[GroupIndex].Add(Goal);
		}
		return true;
	}

	bool AllArrived() const
	{
		for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
			for (int32 Index = 0; Index < Groups[GroupIndex]->Units.Num(); ++Index)
			{
				AArmyUnit* Unit = Groups[GroupIndex]->Units[Index];
				const UPathFollowingComponent* Following = GetFollowing(Unit);
				if (!Goals[GroupIndex].IsValidIndex(Index) || !Following || Following->GetStatus() != EPathFollowingStatus::Idle
					|| !Following->DidMoveReachGoal()
					|| Unit->GetVelocity().Size2D() > 5. || FVector::Dist2D(Unit->GetActorLocation(), Goals[GroupIndex][Index]) > 80.) return false;
			}
		return true;
	}

	bool RejectMove(int32 GroupIndex, const FVector& Target, const TCHAR* Description)
	{
		AArmyGroup* Group = Groups[GroupIndex].Get();
		const uint32 Serial = Group->OrderSerial;
		const EArmyOrder Order = Group->Order;
		const FVector Destination = Group->Destination;
		TArray<FAIRequestID, TInlineAllocator<6>> Requests;
		for (AArmyUnit* Unit : Group->Units) Requests.Add(GetFollowing(Unit)->GetCurrentRequestId());
		if (!Check(!Group->IssueMove(Target), FString::Printf(TEXT("%s request is rejected"), Description))
			|| !Check(Group->OrderSerial == Serial && Group->Order == Order && Group->Destination.Equals(Destination), TEXT("Rejected request preserves accepted order state"))) return false;
		for (int32 Index = 0; Index < Group->Units.Num(); ++Index)
		{
			UPathFollowingComponent* Following = GetFollowing(Group->Units[Index]);
			if (!Check(Following && Following->GetCurrentRequestId() == Requests[Index] && Following->GetStatus() == EPathFollowingStatus::Moving,
				TEXT("Rejected request preserves each active AI request"))) return false;
		}
		return true;
	}


	void RememberPositions(int32 GroupIndex)
	{
		Positions[GroupIndex].Reset();
		for (AArmyUnit* Unit : Groups[GroupIndex]->Units) Positions[GroupIndex].Add(Unit->GetActorLocation());
	}

	void SetStage(EStage Next, double Now) { Stage = Next; StageStarted = Now; }

	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Groups[2];
	TWeakObjectPtr<AArmyGroup> Enemy;
	TArray<FVector> Goals[2];
	TArray<FVector> Positions[2];
	TSet<AArmyUnit*> WentAroundObstacle;
	EStage Stage = EStage::FindGroups;
	int32 ReplacementCount = 0;
	bool bFailed = false;
	bool bIsolated = false;
	double Started;
	double StageStarted = 0.;
};

bool FArmyTwoGroupsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyTwoGroupsScenario(this));
	return true;
}

#endif
