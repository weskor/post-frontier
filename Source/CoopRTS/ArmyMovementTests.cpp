#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "AIController.h"
#include "HAL/PlatformTime.h"
#include "Navigation/PathFollowingComponent.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyTwoGroupsTest, "CoopRTS.Movement.TwoGroups",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Real character paths between generated polygon anchors, not arbitrary ground orders.
class FArmyTwoGroupsScenario : public IAutomationLatentCommand
{
public:
	explicit FArmyTwoGroupsScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 90.)
			return Fail(TEXT("TwoGroups timed out before physical region arrival"));
		if (!Groups[0].IsValid())
		{
			UWorld* World = ArmyTestSetup::World();
			ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
			Controller = World ? ArmyTestSetup::Controller(World) : nullptr;
			if (!ArmyTestSetup::MapReady(State) || !Controller.IsValid() || Now - Started < 3.)
				return false;
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (It->IsOpposingArmy())
					It->Destroy();
			for (ACommandBuilding* Building : State->Buildings)
				if (IsValid(Building) && Building->TeamIndex == 5 && Building->IsProducer())
					FCommandService::ConfigureProduction(State->EnemyCommander, Building,
						Building->bForceConfigured ? Building->ProductionRole : static_cast<EUnitRole>(255), false);
			Home = ArmyTestSetup::RegionAt(State, State->FriendlyHeadquarters->GetActorLocation());
			const FVector Anchor = State->GetRegionAnchor(Home);
			Groups[0] = ArmyTestSetup::SpawnGroup(World, Controller.Get(), 0, Anchor + FVector(0.f, -350.f, 100.f));
			Groups[1] = ArmyTestSetup::SpawnGroup(World, Controller.Get(), 1, Anchor + FVector(0.f, 350.f, 100.f));
			if (!Groups[0].IsValid() || !Groups[1].IsValid())
				return Fail(TEXT("Both independently owned six-member fixtures must spawn"));
			Target = ArmyTestSetup::TravelRegion(Groups[0].Get(), State->EnemyHeadquarters->GetActorLocation());
			if (Target == INDEX_NONE)
				return Fail(TEXT("Generated home polygon needs a reachable non-enemy-main neighbour"));
			StageStarted = Now;
			return false;
		}
		if (!Groups[1].IsValid() || !Controller.IsValid())
			return Fail(TEXT("Both independent forces and their commander must survive"));
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (Stage == 0)
		{
			const EForceVerb OtherVerb = Groups[1]->Verb;
			const int32 OtherTarget = Groups[1]->TargetRegionIndex;
			if (!FCommandService::IssueForceOrder(Wallet, Groups[0].Get(), EForceVerb::MoveHold, Target))
				return false; // Navigation initialization can reject the first route.
			if (Groups[1]->Verb != OtherVerb || Groups[1]->TargetRegionIndex != OtherTarget)
				return Fail(TEXT("Ordering force zero must not change force one's intent"));
			if (!FCommandService::IssueForceOrder(Wallet, Groups[1].Get(), EForceVerb::MoveHold, Target))
				return false;
			Stage = 1;
			StageStarted = Now;
		}
		else if (Stage == 1 && Now - StageStarted >= .12)
		{
			const int32 Replacement = (++Replacements % 2) ? Home : Target;
			for (const TWeakObjectPtr<AArmyGroup>& Force : Groups)
				if (!FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, Replacement))
					return Fail(TEXT("Reachable region replacement must be accepted"));
			StageStarted = Now;
			if (Replacements == 8)
				Stage = 2;
		}
		else if (Stage == 2 && Now - StageStarted >= .5)
		{
			for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
			{
				AArmyGroup* Force = Groups[GroupIndex].Get();
				const uint32 Serial = Force->OrderSerial;
				const FVector Destination = Force->Destination;
				for (AArmyUnit* Unit : Force->GetUnits())
				{
					const UPathFollowingComponent* Path = Following(Unit);
					if (!Path || Path->GetStatus() != EPathFollowingStatus::Moving)
						return Fail(TEXT("Every member must have an active navigation request before rejection"));
					Requests[GroupIndex].Add(Path->GetCurrentRequestId());
					Positions[GroupIndex].Add(Unit->GetActorLocation());
				}
				if (FCommandService::IssueForceOrder(Wallet, Force, EForceVerb::MoveHold, ForceOrders::MaxRegions + 1)
					|| Force->OrderSerial != Serial || Force->TargetRegionIndex != Target || Force->Destination != Destination)
					return Fail(TEXT("Invalid region rejects atomically without replacing accepted travel"));
				for (int32 Index = 0; Index < Force->GetUnits().Num(); ++Index)
					if (Following(Force->GetUnits()[Index])->GetCurrentRequestId() != Requests[GroupIndex][Index])
						return Fail(TEXT("Rejected order must preserve every active character path request"));
			}
			Stage = 3;
			StageStarted = Now;
		}
		else if (Stage == 3 && Now - StageStarted >= .6)
		{
			for (int32 GroupIndex = 0; GroupIndex < 2; ++GroupIndex)
				for (int32 Index = 0; Index < Groups[GroupIndex]->GetUnits().Num(); ++Index)
					if (FVector::Dist2D(Groups[GroupIndex]->GetUnits()[Index]->GetActorLocation(), Positions[GroupIndex][Index]) < 20.f)
						return Fail(TEXT("Rejected intent must not stop any travelling character"));
			Stage = 4;
		}
		else if (Stage == 4)
		{
			const ACommandGameState* State = Groups[0]->GetWorld()->GetGameState<ACommandGameState>();
			for (const TWeakObjectPtr<AArmyGroup>& Force : Groups)
			{
				if (Force->GetUnits().Num() != 6)
					return Fail(TEXT("Navigation must preserve every member's identity"));
				if (Force->Status != EForceStatus::Holding || Force->TargetRegionIndex != Target)
					return false;
				for (const AArmyUnit* Unit : Force->GetUnits())
				{
					if (Unit->GetGroup() != Force.Get())
						return Fail(TEXT("Independent forces cannot exchange members"));
					if (Unit->GetVelocity().Size2D() > 5.f || FVector::Dist2D(Unit->GetActorLocation(), State->GetRegionAnchor(Target)) > 500.f)
						return false;
				}
			}
			Test->AddInfo(TEXT("TwoGroups: independent region orders, eight rapid replacements, atomic rejection with uninterrupted paths, and twelve physical arrivals."));
			return true;
		}
		return false;
	}
private:
	static const UPathFollowingComponent* Following(const AArmyUnit* Unit)
	{
		const AAIController* AI = Cast<AAIController>(Unit->GetController());
		return AI ? AI->GetPathFollowingComponent() : nullptr;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	FAutomationTestBase* Test;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Groups[2];
	TArray<FVector> Positions[2];
	TArray<FAIRequestID> Requests[2];
	int32 Home = INDEX_NONE, Target = INDEX_NONE, Stage = 0, Replacements = 0;
	double Started, StageStarted = 0.;
};

bool FArmyTwoGroupsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyTwoGroupsScenario(this));
	return true;
}
#endif
