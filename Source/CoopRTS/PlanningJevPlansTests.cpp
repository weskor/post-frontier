#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyUnit.h"
#include "EnemyCommander.h"
#include "EngineUtils.h"
#include "PlanningFixture.h"

// JEV's first plans (battle.md "Opening", jev.md "Published intent"): its kit forces are planned and published on
// the frozen world before 0:00, follow the roster, keep their tickets and 25 s commitment through 0:00 and are
// issued as orders then.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPlanningJevPlansWorldTest, "CoopRTS.Planning.JevPlans",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PlanningJevPlansTests
{
struct FRecorded
{
	TWeakObjectPtr<AArmyGroup> Force;
	int32 Ticket = 0;
	int32 Target = INDEX_NONE;
	EForceVerb Verb = EForceVerb::MoveHold;
	float CommittedUntil = 0.f;
};

class FScenario final : public PlanningFixture::FScenario
{
public:
	using PlanningFixture::FScenario::FScenario;

private:
	bool Step() override
	{
		if (bWatching && !Continuous())
			return Done();
		switch (Stage)
		{
		case 0:
			return FirstPlans();
		case 1:
			return Frozen();
		case 2:
			return Joined();
		case 3:
			return Left();
		case 4:
			return Started();
		case 5:
			return FirstUnit();
		case 6:
			return Latched();
		default:
			return Executing();
		}
	}

	const FJevPublishedPlan* PlanOf(const AArmyGroup* Force) const
	{
		return State->EnemyPlans.FindByPredicate([Force](const FJevPublishedPlan& Plan) { return Plan.Force == Force; });
	}

	bool KitPlansStand(int32 Count) const
	{
		if (State->Planning.JevKits.Num() != Count || State->EnemyPlans.Num() != Count)
			return false;
		for (const FPlanningKit& Kit : State->Planning.JevKits)
			if (!IsValid(Kit.Barracks) || !Kit.Barracks->IsComplete() || !IsValid(Kit.Barracks->ForceGroup) || !PlanOf(Kit.Barracks->ForceGroup))
				return false;
		return true;
	}

	bool WaitForPlans(int32 Count, const TCHAR* What)
	{
		if (KitPlansStand(Count))
			return true;
		if (StageSeconds() > 20.)
		{
			Check(false, FString::Printf(TEXT("JEV's %d kit forces have published plans (%s): kits %d, plans %d"), Count, What, State->Planning.JevKits.Num(), State->EnemyPlans.Num()));
			Done();
		}
		return false;
	}

	// Every kit force of JEV is empty and producer-backed, holds only its idle rally, and has a published plan.
	bool CheckPlanned(const TCHAR* What)
	{
		TSet<const AArmyGroup*> Distinct;
		for (const FPlanningKit& Kit : State->Planning.JevKits)
		{
			const AArmyGroup* Force = Kit.Barracks->ForceGroup;
			const FJevPublishedPlan* Plan = PlanOf(Force);
			Distinct.Add(Force);
			if (!Check(Kit.Barracks->bForceConfigured && Force->GetProductionBuilding() == Kit.Barracks && Force->GetAliveCount() == 0
						&& Force->Orders.Num() <= 1 && (Force->Orders.IsEmpty() || Force->Orders[0].RegionIndex == Kit.Barracks->RallyRegionIndex)
						&& Force->GetOwningPlayerState() == State->EnemyCommander,
					FString::Printf(TEXT("%s: the kit force is JEV's, producer-backed, empty and has no order yet"), What))
				|| !Check(Plan && Plan->TicketNumber > 0 && Plan->TargetRegionIndex != INDEX_NONE && !Plan->Memo.IsEmpty() && Plan->SizeBand >= 2,
					FString::Printf(TEXT("%s: the plan has a ticket, a target, a size band and a memo"), What))
				|| !Check(FMath::IsNearlyEqual(Plan->RemainingCommitment, 25.f, .05f)
						&& FMath::IsNearlyEqual(Plan->CommittedUntil, static_cast<float>(World->GetTimeSeconds()) + 25.f, .05f),
					FString::Printf(TEXT("%s: the 25 s commitment runs from the frozen clock (remaining %.2f)"), What, Plan->RemainingCommitment)))
				return false;
		}
		return Check(Distinct.Num() == State->EnemyPlans.Num(), FString::Printf(TEXT("%s: one plan per force"), What));
	}

	void Record()
	{
		Plans.Reset();
		for (const FJevPublishedPlan& Plan : State->EnemyPlans)
			Plans.Add({ Plan.Force, Plan.TicketNumber, Plan.TargetRegionIndex, Plan.Verb, Plan.CommittedUntil });
		PlanningClock = static_cast<float>(World->GetTimeSeconds());
	}

	// The recorded forces still carry the same plan: same ticket, target, verb and deadline.
	bool SamePlans(const TCHAR* What)
	{
		for (const FRecorded& Was : Plans)
		{
			const FJevPublishedPlan* Now = Was.Force.IsValid() ? PlanOf(Was.Force.Get()) : nullptr;
			if (!Check(Now && Now->TicketNumber == Was.Ticket && Now->TargetRegionIndex == Was.Target && Now->Verb == Was.Verb
						&& Now->CommittedUntil == Was.CommittedUntil,
					FString::Printf(TEXT("%s: force %d keeps its ticket %d, target and deadline"), What, Was.Force.IsValid() ? Was.Force->ForceNumber : -1, Was.Ticket)))
				return false;
		}
		return true;
	}

	// Sampled on every tick from the last Ready through 0:00 and the first units: each kit force's plan never leaves
	// the published list, under its recorded ticket (the planning-ui diagnosis: it vanished for ~3 s after 0:00).
	bool Continuous()
	{
		for (const FRecorded& Was : Plans)
		{
			const FJevPublishedPlan* Now = Was.Force.IsValid() ? PlanOf(Was.Force.Get()) : nullptr;
			if (!Check(Now && Now->TicketNumber == Was.Ticket,
					FString::Printf(TEXT("Force %d's ticket %d stays published on every tick (%s, game %.2f s, units %d)"), Was.Force.IsValid() ? Was.Force->ForceNumber : -1,
						Was.Ticket, State->IsPlanning() ? TEXT("planning") : TEXT("after 0:00"), World->GetTimeSeconds(), Was.Force.IsValid() ? Was.Force->GetAliveCount() : -1)))
				return false;
		}
		return true;
	}

	bool FirstPlans()
	{
		if (!WaitForPlans(2, TEXT("two humans")))
			return false;
		if (!CheckPlanned(TEXT("Before 0:00")))
			return Done();
		if (!Check(State->EnemyCommander->Resources == 200 - Spent(), TEXT("Configuring the kit forces cost JEV only their configuration fee")))
			return Done();
		Record();
		Enter(1);
		return false;
	}

	// The planner is not driven by a paused tick: the clock and the plans stand still while planning runs.
	bool Frozen()
	{
		if (StageSeconds() < 1.5)
			return false;
		if (!Check(FMath::IsNearlyEqual(static_cast<float>(World->GetTimeSeconds()), PlanningClock, .001f) && SamePlans(TEXT("After 1.5 s of planning"))
					&& CheckPlanned(TEXT("After 1.5 s of planning")),
				TEXT("The frozen world re-plans nothing")))
			return Done();
		ThirdPtr = SpawnCommander();
		if (!Check(ThirdPtr.IsValid(), TEXT("A third commander joins")))
			return Done();
		Enter(2);
		return false;
	}

	bool Joined()
	{
		if (!WaitForPlans(3, TEXT("a third human joined")))
			return false;
		if (!SamePlans(TEXT("A third kit joined")) || !CheckPlanned(TEXT("A third kit joined"))
			|| !Check(State->EnemyCommander->Resources == 200 - Spent(), TEXT("The third kit's force costs JEV its configuration fee")))
			return Done();
		RemoveCommander(ThirdPtr.Get());
		Enter(3);
		return false;
	}

	bool Left()
	{
		if (!WaitForPlans(2, TEXT("the third human left")))
			return false;
		if (!SamePlans(TEXT("The third kit left")) || !CheckPlanned(TEXT("The third kit left"))
			|| !Check(State->EnemyCommander->Resources == 200 - Spent(), TEXT("A removed kit force gives its fee back")))
			return Done();
		TArray<ACommandPlayerState*, TInlineAllocator<3>> Humans;
		for (const FPlanningKit& Kit : State->Planning.Kits)
			Humans.Add(Kit.Commander);
		for (ACommandPlayerState* Human : Humans)
			if (!Check(FPlanningCommands::SetReady(Human, true).IsAccepted(), TEXT("Ready is accepted")))
				return Done();
		bWatching = true;
		Enter(4);
		return false;
	}

	// 0:00: planning has ended and JEV's first evaluation has run. The plans are the ones published before, now orders.
	bool Started()
	{
		if (State->IsPlanning())
			return false;
		ZeroClock = static_cast<float>(World->GetTimeSeconds());
		if (!Check(ZeroClock - PlanningClock < .5f, TEXT("0:00 follows planning without the planning time passing on the world clock"))
			|| !Check(State->EnemyPlans.Num() == 2 && SamePlans(TEXT("At 0:00")), TEXT("The published plans survive 0:00 under their tickets")))
			return Done();
		for (const FRecorded& Was : Plans)
		{
			const AArmyGroup* Force = Was.Force.Get();
			const FJevPublishedPlan* Plan = PlanOf(Force);
			if (!Check(Force && Force->Orders.Num() == 1 && Force->Orders[0].Verb == Was.Verb && Force->Verb == Was.Verb
						&& (Was.Verb == EForceVerb::Retreat || (Force->Orders[0].RegionIndex == Was.Target && Force->TargetRegionIndex == Was.Target)),
					TEXT("At 0:00 each kit force is ordered what it planned"))
				|| !Check(FMath::IsNearlyEqual(Plan->RemainingCommitment, 25.f, .05f),
					FString::Printf(TEXT("The 25 s commitment starts at 0:00 (remaining %.2f)"), Plan->RemainingCommitment)))
				return Done();
		}
		Enter(5);
		return false;
	}

	// The first unit of any kit force: its ticket has stood on every tick up to here (Continuous). The force is lost
	// next frame, so no evaluation sees it alive.
	bool FirstUnit()
	{
		for (const FRecorded& Was : Plans)
			if (Was.Force.IsValid() && Was.Force->GetAliveCount() > 0)
			{
				Lost = Was.Force;
				Enter(6);
				return false;
			}
		if (StageGameSeconds() > 20.)
		{
			Check(false, TEXT("A kit force of JEV fields its first unit"));
			return Done();
		}
		return false;
	}

	// A first unit lost before JEV's next evaluation still makes its force a fielded one: the plan goes with the
	// explicit evaluation below, and does not stay up as for a force that never fielded.
	bool Latched()
	{
		AArmyGroup* Victim = Lost.Get();
		if (!Check(Victim && Victim->GetAliveCount() > 0 && PlanOf(Victim), TEXT("The force with a first unit has its plan before the unit is lost")))
			return Done();
		const TArray<TObjectPtr<AArmyUnit>> Units = Victim->GetUnits();
		for (AArmyUnit* Unit : Units)
			if (IsValid(Unit))
				Unit->Destroy();
		EvaluateJev();
		if (!Check(Victim->GetAliveCount() == 0 && !PlanOf(Victim), TEXT("A first unit lost between evaluations still fields its force: no plan while it is empty")))
			return Done();
		Plans.RemoveAll([Victim](const FRecorded& Was) { return Was.Force == Victim; });
		Enter(7);
		return false;
	}

	void EvaluateJev()
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			if (It->TeamIndex == 5)
				It->EvaluatePlan();
	}

	// The other kit forces keep their plans and tickets until each has fielded its first unit, and after it.
	bool Executing()
	{
		for (const FRecorded& Was : Plans)
			if (Was.Force.IsValid() && Was.Force->GetAliveCount() == 0)
			{
				if (StageGameSeconds() > 25.)
				{
					Check(false, TEXT("Every other kit force of JEV fields its first unit"));
					return Done();
				}
				return false;
			}
		if (StageGameSeconds() < 6.)
			return false;
		if (!SamePlans(TEXT("After every kit force fielded")))
			return Done();
		return Wiped();
	}

	// A force wiped out is not planned while it refills, as before: its plan and ticket go until a unit returns.
	bool Wiped()
	{
		AArmyGroup* Victim = Plans.IsEmpty() ? nullptr : Plans[0].Force.Get();
		if (!Check(Victim && Victim->GetAliveCount() > 0 && PlanOf(Victim), TEXT("A fielded kit force has a plan before it is wiped out")))
			return Done();
		bWatching = false;
		const TArray<TObjectPtr<AArmyUnit>> Units = Victim->GetUnits();
		for (AArmyUnit* Unit : Units)
			if (IsValid(Unit))
				Unit->Destroy();
		EvaluateJev();
		Check(Victim->GetAliveCount() == 0 && !PlanOf(Victim), TEXT("A wiped-out force keeps no plan while it has no unit"));
		return Done();
	}

	// The configuration fees of JEV's kit forces so far.
	int32 Spent() const
	{
		int32 Fees = 0;
		for (const FPlanningKit& Kit : State->Planning.JevKits)
			if (const UArmyUnitDefinition* Unit = IsValid(Kit.Barracks) ? Kit.Barracks->GetProductionDefinition() : nullptr)
				Fees += ACommandBuilding::GetConfigurationCost(*Unit);
		return Fees;
	}

	TArray<FRecorded> Plans;
	TWeakObjectPtr<ACommandPlayerState> ThirdPtr;
	TWeakObjectPtr<AArmyGroup> Lost;
	float PlanningClock = 0.f;
	float ZeroClock = 0.f;
	bool bWatching = false;

	void Cleanup() override
	{
		if (ThirdPtr.IsValid())
			RemoveCommander(ThirdPtr.Get());
	}
};
}

bool FPlanningJevPlansWorldTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PlanningJevPlansTests::FScenario(this));
	return true;
}

#endif
