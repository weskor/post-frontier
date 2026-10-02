#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

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
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 35.)
		{
			if (RejectedInitialTarget.IsSet())
				Test->AddError(FString::Printf(TEXT("Timed out waiting for live army navigation: initial Move rejected at %s"),
					*RejectedInitialTarget.GetValue().ToString()));
			else
				Test->AddError(TEXT("Timed out waiting for live army navigation"));
			return true;
		}
		if (Stage == 0)
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
			if (!Army.IsValid() || Now - Started < 3.)
				return false;
			StartCenter = Army->GetCenter();
			Serial = Army->OrderSerial;
			const FVector InitialTarget = Army->GetHomeLocation() + FVector(0.f, 1800.f, 0.f);
			FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Move, InitialTarget);
			if (Army->OrderSerial == Serial)
			{
				RejectedInitialTarget = InitialTarget;
				return false; // Navmesh can still be generating.
			}
			RejectedInitialTarget.Reset();
			NextStage(Now);
			return false;
		}
		if (!Army.IsValid() || !Controller.IsValid())
		{
			Test->AddError(TEXT("Army or controller disappeared during orders"));
			return true;
		}
		if (Stage == 1 && Now - StageStarted >= 1.5)
		{
			Test->TestTrue(TEXT("Units actually move under the initial order"), FVector::Dist2D(StartCenter, Army->GetCenter()) > 100.);
			StartCenter = Army->GetCenter();
			Replacement = Army->GetHomeLocation() + FVector(-1200.f, -1500.f, 0.f);
			Serial = Army->OrderSerial;
			FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Move, Replacement);
			Test->TestTrue(TEXT("Replacement receives a new order serial"), Army->OrderSerial > Serial);
			Test->TestTrue(TEXT("Replacement records the new destination"), FVector::Dist2D(Army->Destination, Replacement) < 100.);
			NextStage(Now);
		}
		else if (Stage == 2 && Now - StageStarted >= 1.5)
		{
			Test->TestTrue(TEXT("Units approach the replacement rather than stale intent"), FVector::Dist2D(Army->GetCenter(), Replacement) + 100. < FVector::Dist2D(StartCenter, Replacement));
			FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Hold, Army->GetCenter());
			Test->TestTrue(TEXT("Hold replaces movement state"), Army->Order == EArmyOrder::Hold);
			NextStage(Now);
		}
		else if (Stage == 3 && Now - StageStarted >= .5)
		{
			for (AArmyUnit* Unit : Army->GetUnits())
				HeldPositions.Add(Unit->GetActorLocation());
			Serial = Army->OrderSerial;
			const ACommandGameState* State = Army->GetWorld()->GetGameState<ACommandGameState>();
			FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Move, ArmyTestSetup::OutsideArena(State));
			Test->TestEqual(TEXT("Out-of-bounds request preserves the accepted order"), Army->OrderSerial, Serial);
			NextStage(Now);
		}
		else if (Stage == 4 && Now - StageStarted >= 1.)
		{
			for (int32 Index = 0; Index < Army->GetUnits().Num(); ++Index)
				Test->TestTrue(TEXT("Every unit stays stopped after Hold"), FVector::Dist2D(Army->GetUnits()[Index]->GetActorLocation(), HeldPositions[Index]) < 5.);
			FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Retreat, Army->GetCenter());
			Test->TestTrue(TEXT("Retreat replaces Hold"), Army->Order == EArmyOrder::Retreat);
			NextStage(Now);
		}
		else if (Stage == 5 && FVector::Dist2D(Army->GetCenter(), Army->GetHomeLocation()) < 150.)
		{
			FCommandService::IssueOrder(Controller->GetPlayerState<ACommandPlayerState>(), Army.Get(), EArmyOrder::Hold, Army->GetCenter());
			Test->AddInfo(TEXT("Live navigation passed: initial move, replacement, individual unit Hold, invalid destination, retreat home."));
			return true;
		}
		return false;
	}
private:
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
	FVector Replacement = FVector::ZeroVector;
	TOptional<FVector> RejectedInitialTarget;
	uint32 Serial = 0;
	int32 Stage = 0;
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
