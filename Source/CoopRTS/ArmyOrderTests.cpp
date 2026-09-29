#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandPlayerController.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyReplacementTest, "CoopRTS.Orders.ReplaceHoldRetreat",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Runs against real characters, AI controllers and the arena navmesh in a running game.
// Use a fresh Boot map; this scenario deliberately moves the local player's army.
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
						for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
						bIsolated = true; // Keep this historical order scenario independent of strategic AI.
						break;
					}
		}
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 35.) { Test->AddError(TEXT("Timed out waiting for live army navigation")); return true; }
		if (Stage == 0)
		{
			for (const FWorldContext& Context : GEngine->GetWorldContexts())
			{
				UWorld* World = Context.World();
				if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_Client) continue;
				for (TActorIterator<AArmyGroup> It(World); It; ++It)
				{
					if (auto* Owner = Cast<ACommandPlayerController>(It->GetOwner()))
					{
						if (Owner->IsLocalController() && It->ArmyIndex == 0 && It->Units.Num() == 6) { Army = *It; Controller = Owner; break; }
					}
				}
			}
			if (!Army.IsValid() || Now - Started < 3.) return false;
			StartCenter = Army->GetCenter();
			Serial = Army->OrderSerial;
			Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Move, FVector(-1800, 1800, 0));
			if (Army->OrderSerial == Serial) return false; // Navmesh can still be generating.
			NextStage(Now);
			return false;
		}
		if (!Army.IsValid() || !Controller.IsValid()) { Test->AddError(TEXT("Army or controller disappeared during orders")); return true; }
		if (Stage == 1 && Now - StageStarted >= 1.5)
		{
			Test->TestTrue(TEXT("Units actually move under the initial order"), FVector::Dist2D(StartCenter, Army->GetCenter()) > 100.);
			StartCenter = Army->GetCenter();
			Replacement = FVector(-3000, -1500, 0);
			Serial = Army->OrderSerial;
			Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Move, Replacement);
			Test->TestTrue(TEXT("Replacement receives a new order serial"), Army->OrderSerial > Serial);
			Test->TestTrue(TEXT("Replacement records the new destination"), FVector::Dist2D(Army->Destination, Replacement) < 100.);
			NextStage(Now);
		}
		else if (Stage == 2 && Now - StageStarted >= 1.5)
		{
			Test->TestTrue(TEXT("Units approach the replacement rather than stale intent"), FVector::Dist2D(Army->GetCenter(), Replacement) + 100. < FVector::Dist2D(StartCenter, Replacement));
			Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Hold, FVector::ZeroVector);
			Test->TestTrue(TEXT("Hold replaces movement state"), Army->Order == EArmyOrder::Hold);
			NextStage(Now);
		}
		else if (Stage == 3 && Now - StageStarted >= .5)
		{
			for (AArmyUnit* Unit : Army->Units) HeldPositions.Add(Unit->GetActorLocation());
			Serial = Army->OrderSerial;
			Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Move, FVector(100000, 0, 0));
			Test->TestEqual(TEXT("Out-of-bounds request preserves the accepted order"), Army->OrderSerial, Serial);
			NextStage(Now);
		}
		else if (Stage == 4 && Now - StageStarted >= 1.)
		{
			for (int32 Index = 0; Index < Army->Units.Num(); ++Index)
				Test->TestTrue(TEXT("Every unit stays stopped after Hold"), FVector::Dist2D(Army->Units[Index]->GetActorLocation(), HeldPositions[Index]) < 5.);
			Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Retreat, FVector::ZeroVector);
			Test->TestTrue(TEXT("Retreat replaces Hold"), Army->Order == EArmyOrder::Retreat);
			NextStage(Now);
		}
		else if (Stage == 5 && FVector::Dist2D(Army->GetCenter(), Army->HomeLocation) < 150.)
		{
			Controller->ServerIssueOrder(Army.Get(), EArmyOrder::Hold, FVector::ZeroVector);
			Test->AddInfo(TEXT("Live navigation passed: initial move, replacement, individual unit Hold, invalid destination, retreat home."));
			return true;
		}
		return false;
	}
private:
	void NextStage(double Now) { ++Stage; StageStarted = Now; }
	FAutomationTestBase* Test;
	TWeakObjectPtr<AArmyGroup> Army;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TArray<FVector> HeldPositions;
	FVector StartCenter = FVector::ZeroVector;
	FVector Replacement = FVector::ZeroVector;
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
