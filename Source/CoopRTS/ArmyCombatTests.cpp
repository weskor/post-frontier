#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyCombatScenario.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyCombatTest, "CoopRTS.Combat.Encounter",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace ArmyCombatScenarioPrivate
{
bool FArmyCombatScenario::Update()
{
	IsolateWorld();
	if (FPlatformTime::Seconds() - Started > 65.)
	{
		Test->AddError(FString::Printf(TEXT("Combat encounter timed out at stage %d"), Stage));
		return true;
	}
	if (Stage == 0)
		return Begin();
	if (!Check(Army.IsValid() && Enemy.IsValid() && Controller.IsValid(), TEXT("Encounter groups and owner remain valid")))
		return true;
	const double Now = ArmyTestSetup::GameSeconds(Army->GetWorld());
	if (Stage == 3)
		return MoveHoldStage(Now);
	if (Stage == 1)
		return AttackStage(Now);
	return RetreatStage(Now);
}

void FArmyCombatScenario::IsolateWorld()
{
	if (!bIsolated)
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (UWorld* World = Context.World())
				if (World->IsGameWorld() && World->GetNetMode() == NM_Standalone)
				{
					for (TActorIterator<AEnemyCommander> It(World); It; ++It)
						It->Destroy();
					bIsolated = true; // The fixture, not the enemy planner, owns encounter orders.
					break;
				}
	}
}
}

bool FArmyCombatTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(ArmyCombatScenarioPrivate::FArmyCombatScenario(this));
	return true;
}

#endif
