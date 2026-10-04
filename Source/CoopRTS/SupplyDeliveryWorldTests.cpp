#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SupplyDeliveryFixture.h"
#include "GroundHeight.h"

// Replacements travel along the supply chain: a force two hops from the producer's region gets each
// recruit after 4 s + 2 s per hop at its own formation, a cut force's recruit waits at the producer,
// and a dead producer takes its queued recruits and pays their Power back.

namespace SupplyTests
{
// Production queues one recruit; nothing walks, and the member count only rises when the delay ends.
class FTwoHops : public FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;

protected:
	bool Run() override
	{
		if (Stage < 2)
		{
			if (!PutForceInFar())
				return false;
			SetStage(2);
		}
		if (Stage == 2)
			return !Queue();
		if (Joined() == 1)
		{
			// Nothing exists at the front or on the way; the recruit is only a queue entry.
			return !Check(MemberActors() == 1 && Force->GetUnits().Num() == 1 && Force->RecruitsInTransit == 1 && StageSeconds() < 9.,
				TEXT("Until the delay ends the recruit is only queued: no actor, still in transit, delivered within the delay"));
		}
		return Arrived();
	}

private:
	bool Queue()
	{
		const int32 Paid = Produce();
		int32 ProducerJoined = 0, ProducerTravelling = 0;
		Producer->GetForceCounts(ProducerJoined, ProducerTravelling);
		if (Check(Paid == UnitCost() && Force->RecruitsInTransit == 1 && Force->RecruitsWaiting == 0 && !Force->bSupplyCutOff
					&& Joined() == 1 && MemberActors() == 1 && ProducerJoined == 1 && ProducerTravelling == 1,
				TEXT("A finished recruit is paid once and counted as travelling, never as joined strength")))
			SetStage(3);
		return Stage == 3;
	}
	bool Arrived()
	{
		const double Elapsed = StageSeconds();
		const AArmyUnit* First = FirstMember();
		const AArmyUnit* Recruit = nullptr;
		for (const AArmyUnit* Unit : Force->GetUnits())
			if (IsValid(Unit) && Unit != First)
				Recruit = Unit;
		Check(Elapsed >= 8. && Elapsed <= 8.6, TEXT("A force two hops out receives its recruit after 4 s + 2 s per hop"));
		Check(Recruit && Joined() == 2 && MemberActors() == 2 && Force->GetPendingRecruitCount() == 0 && InRegion(Recruit, Far)
				&& FVector::Dist2D(Recruit->GetActorLocation(), First->GetActorLocation()) < 500.f
				&& FVector::Dist2D(Recruit->GetActorLocation(), Producer->GetActorLocation()) > 2500.f
				&& FMath::Abs(Recruit->GetActorLocation().Z - GroundHeight::At(*GameWorld, Recruit->GetActorLocation().X, Recruit->GetActorLocation().Y) - 60.) < 40.,
			TEXT("The recruit appears at the force's formation, in Far, on the ground height, and joins at once"));
		return true;
	}
};

// Cutting the chain holds the recruit at the producer and production at one; reconnecting restarts the delay.
class FCutAndReconnect : public FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;

protected:
	bool Run() override
	{
		if (Stage < 2)
		{
			if (!PutForceInFar())
				return false;
			SetStage(2);
		}
		switch (Stage)
		{
		case 2:
			if (!Check(Produce() == UnitCost() && Force->RecruitsInTransit == 1, TEXT("The first recruit is paid and starts in transit")))
				return true;
			SetStage(3);
			return false;
		case 3:
			if (StageSeconds() >= 3.)
			{
				Fixture->SetController(Neck, 5);
				SetStage(4);
			}
			return false;
		case 4:
			return Held();
		case 5:
			// However long the cut lasts, the recruit stays at the producer and nothing walks.
			if (!Check(Joined() == 1 && MemberActors() == 1 && Force->RecruitsWaiting == 1 && Force->bSupplyCutOff,
					TEXT("The held recruit never walks to a cut-off force")))
				return true;
			if (StageSeconds() >= 20.)
			{
				Fixture->SetController(Neck, 0);
				ReconnectedAt = Now();
				SetStage(6);
			}
			return false;
		default:
			return Delivered();
		}
	}

private:
	bool Held()
	{
		if (State->IsRegionConnected(0, Far) || Force->RecruitsWaiting != 1)
			return !Check(StageSeconds() < 2., TEXT("Cutting the neck cuts Far off and sends the recruit back to the producer"));
		// An enabled producer with a recruit waiting does no new work: no progress, no debit.
		const int32 Balance = Wallet->Resources;
		FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, true);
		const EProductionState Enabled = Producer->GetProductionState();
		for (int32 Attempt = 0; Attempt < 3; ++Attempt)
			Producer->TickProduction(Producer->GetProductionDuration());
		FCommandService::ConfigureProduction(Wallet, Producer.Get(), EUnitRole::Frontline, false);
		int32 ProducerJoined = 0, ProducerTravelling = 0;
		Producer->GetForceCounts(ProducerJoined, ProducerTravelling);
		if (!Check(Wallet->Resources == Balance && Force->RecruitsWaiting == 1 && Force->RecruitsInTransit == 0
					&& Enabled == EProductionState::Held && ProducerTravelling == 1,
				TEXT("Production holds at one waiting recruit and debits nothing more")))
			return true;
		SetStage(5);
		return false;
	}
	bool Delivered()
	{
		const double Elapsed = Now() - ReconnectedAt;
		if (Joined() == 1)
			return !Check(Elapsed < 9.5 && MemberActors() == 1, TEXT("A reconnected force receives the waiting recruit once the full delay has run again"));
		Check(Elapsed >= 8. && Elapsed <= 9.2 && Joined() == 2 && Force->GetPendingRecruitCount() == 0 && !Force->bSupplyCutOff,
			TEXT("Reconnecting restarts the whole 8 s delay before delivery"));
		const int32 Balance = Wallet->Resources;
		Check(Produce() == UnitCost() && Wallet->Resources == Balance - UnitCost() && Force->RecruitsInTransit == 1,
			TEXT("Delivery releases production, which pays and ships the next recruit"));
		return true;
	}

	double ReconnectedAt = 0.;
};

// A dead producer cancels in-transit or waiting recruits and gives their Power back; its force is an orphan.
class FProducerDeath : public FScenarioBase
{
public:
	FProducerDeath(FAutomationTestBase* InTest, bool bInCutFirst) : FScenarioBase(InTest), bCutFirst(bInCutFirst) {}

protected:
	bool Run() override
	{
		if (Stage < 2)
		{
			if (!PutForceInFar())
				return false;
			SetStage(2);
		}
		if (Stage == 2)
		{
			Before = Wallet->Resources;
			Produce();
			Produce();
			if (!Check(Wallet->Resources == Before - 2 * UnitCost() && Force->RecruitsInTransit == 2, TEXT("Two recruits are paid and in transit")))
				return true;
			if (bCutFirst)
				Fixture->SetController(Neck, 5);
			SetStage(3);
			return false;
		}
		if (Stage == 3)
		{
			if (StageSeconds() < 1.)
				return false;
			if (!Check(Force->RecruitsWaiting == (bCutFirst ? 2 : 0) && Force->RecruitsInTransit == (bCutFirst ? 0 : 2),
					TEXT("The recruits are held or in transit as the chain dictates")))
				return true;
			Producer->Destroy();
			if (!Check(Wallet->Resources == Before && Force->GetPendingRecruitCount() == 0 && !IsValid(Force->GetProductionBuilding())
						&& Joined() == 1,
					TEXT("The producer's death refunds exactly what its queued recruits cost and leaves an orphan")))
				return true;
			SetStage(4);
			return false;
		}
		return !Check(Joined() == 1 && MemberActors() == 1 && Wallet->Resources == Before && Force->GetPendingRecruitCount() == 0, TEXT("An orphan receives nothing"))
			|| StageSeconds() >= 12.;
	}

private:
	bool bCutFirst;
	int32 Before = 0;
};

// A force wiped out while a recruit is on its way gets that recruit at the producer's exit, not in Far.
class FWipedForce : public FScenarioBase
{
public:
	using FScenarioBase::FScenarioBase;

protected:
	bool Run() override
	{
		if (Stage < 2)
		{
			if (!PutForceInFar())
				return false;
			SetStage(2);
		}
		if (Stage == 2)
		{
			Before = Wallet->Resources;
			Produce();
			AArmyUnit* Victim = FirstMember();
			Victim->ReceiveAttack((Victim->GetHealth() + Victim->GetShield()) * 10, Attacker.Get());
			if (!Check(Before - Wallet->Resources == UnitCost() && !Victim->IsAlive() && Force->GetAliveCount() == 0,
					TEXT("The only member dies with one recruit paid and in transit")))
				return true;
			SetStage(3);
			return false;
		}
		if (Joined() == 0)
			return !Check(StageSeconds() < 2., TEXT("The wiped force's recruit leaves the producer's exit at once, not after the delivery delay"));
		const AArmyUnit* Recruit = FirstMember();
		Check(Joined() == 1 && Force->GetPendingRecruitCount() == 0 && Before - Wallet->Resources == UnitCost()
				&& FVector::Dist2D(Recruit->GetActorLocation(), Producer->GetActorLocation()) < 1200.f,
			TEXT("The recruit stands at the producer exit, joined, without a second debit"));
		return true;
	}

private:
	int32 Before = 0;
};
}

using namespace SupplyTests;
SUPPLY_WORLD_TEST(FSupplyTwoHopsTest, "TwoHops", FTwoHops(this))
SUPPLY_WORLD_TEST(FSupplyCutAndReconnectTest, "CutAndReconnect", FCutAndReconnect(this))
SUPPLY_WORLD_TEST(FSupplyProducerDeathTest, "ProducerDeathInTransit", FProducerDeath(this, false))
SUPPLY_WORLD_TEST(FSupplyProducerDeathWaitingTest, "ProducerDeathWaiting", FProducerDeath(this, true))
SUPPLY_WORLD_TEST(FSupplyWipedForceTest, "WipedForce", FWipedForce(this))
#endif
