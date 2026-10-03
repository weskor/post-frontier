#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevPlannerWorldFixture.h"
#include "DepositSite.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevCommitmentWorldTest, "CoopRTS.Enemy.Planner.Commitment",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FJevPlannerWorldScenario::PrepareHealth(UWorld* World, ACommandGameState* State, ACommandPlayerController* PC)
{
	// Exhaust every real deposit: the economic world summary changes, but
	// none of the still-neutral destination regions becomes invalid.
	for (ADepositSite* Deposit : State->Deposits)
		if (IsValid(Deposit))
			Deposit->Remaining = 0;
	AArmyGroup* Damager = ArmyTestSetup::SpawnGroup(World, PC, 42,
		ArmyTestSetup::FromFriendlyHQ(State, -250.f, 700.f, 100.f));
	if (!Damager)
		return Fail(TEXT("Real hostile damage fixture must spawn"));
	for (AArmyUnit* Unit : Forces[0]->GetUnits())
		Unit->ReceiveAttack(Unit->GetHealth() - FMath::Max(1, Unit->MaxHealth() / 4), Damager->GetUnits()[0]);
	Damager->Destroy(); // No live attacker/source invasion exception remains.
	if (!Check(Forces[0]->GetAliveCount() == 6 && Forces[0]->GetJoinedCount() == 6
				&& JoinedHealth(*Forces[0]) < .35f && JoinedHealth(*Forces[1]) == 1.f,
			TEXT("Health scores change below recovery threshold without casualties or damage to the independent force")))
		return true;
	return false;
}

bool FJevPlannerWorldScenario::Commitment(ACommandGameState* State, float Now)
{
	if (Now < Initial[0].CommittedUntil)
	{
		LastHeldAt = Now;
		return !Unchanged(State, 0) || !Unchanged(State, 1);
	}
	const FJevPublishedPlan* Fresh = Plan(State, 0);
	if (!Check(LastHeldAt >= Initial[0].CommittedUntil - .5f
				&& Now - AcceptedAt >= 25.f && Fresh->TicketNumber != Initial[0].TicketNumber
				&& Fresh->CommittedUntil > Initial[0].CommittedUntil
				&& (Fresh->Verb == EForceVerb::Retreat
					|| (Fresh->Verb == EForceVerb::MoveHold && State->GetRegionController(Fresh->TargetRegionIndex) == 5)),
			TEXT("Actual order survives the entire 25 seconds despite changed scores, then a fresh ticket accepts health-driven safe recovery")))
		return true;
	Test->AddInfo(TEXT("JEV commitment: full server-time window, real accepted order, changed economics/health, independent force and faithful publication/memos."));
	return true;
}

bool FJevCommitmentWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FJevPlannerWorldScenario(this, EJevWorldProof::Commitment));
	return true;
}
#endif
