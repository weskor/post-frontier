#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevReleaseWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseLostTargetTest, "CoopRTS.Enemy.Release.LostTarget",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseFightsOnWorldTest, "CoopRTS.Enemy.Release.FightsOn",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

// A release that finds no region to raid launches nothing, and its budget joins the next wave.
class FLostTargetScenario : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		// With the human HQ outside every region, JEV cannot name the human main.
		HqHome = Kit.State->FriendlyHeadquarters->GetActorLocation();
		Kit.State->FriendlyHeadquarters->SetActorLocation(FVector(1.0e7f, 1.0e7f, HqHome.Z));
		SkipTo(119.2f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		switch (Stage)
		{
		case 1:
			SkipTo(120.8f);
			Enter(2);
			return false;
		case 2:
			if (InStage() < 1.5)
				return false;
			if (!Check(Release().WaveCount == 0 && EnemyForces(Kit.World).IsEmpty(),
					TEXT("A release with no region to attack launches no wave")))
				return true;
			Kit.State->FriendlyHeadquarters->SetActorLocation(HqHome);
			SkipTo(239.2f);
			Enter(3);
			return false;
		case 3:
			SkipTo(240.8f);
			Enter(4);
			return false;
		default:
			if (!WaveLaunched(1))
				return false;
			const FJevWaveEvent& Event = Release().Waves.Last();
			// The lost v1.1 budget of 150 carries into v1.2's 250.
			Check(Event.Release == 2 && Event.Budget == 400 && Event.Units == 20,
				TEXT("The budget of the release that could not launch carries into the next wave"));
			return true;
		}
	}

	FVector HqHome = FVector::ZeroVector;
};

// A free wave force has no producer to refill it, so badly hurt it still holds its orders.
class FFightsOnScenario : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		AArmyGroup* Humans = ArmyTestSetup::SpawnGroup(Kit.World, Kit.PC, 30,
			ArmyTestSetup::FromFriendlyHQ(Kit.State, -250.f, 350.f, 100.f));
		if (!Humans || Humans->GetUnits().IsEmpty())
			return Fail(TEXT("Human fixture squad could not spawn"));
		Park(*Humans);
		Attacker = Humans->GetUnits()[0];
		SkipTo(119.2f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		if (Stage == 1)
		{
			SkipTo(120.8f);
			Enter(2);
			return false;
		}
		if (Stage == 2)
			return WaveLaunched(1) ? Wound() : false;
		// Past the 25 s commitment the ordinary planner is back in charge of the wave.
		if (InStage() < 32.)
			return false;
		for (const AArmyGroup* Force : EnemyForces(Kit.World))
		{
			const FJevPublishedPlan* Plan = PlanOf(Force);
			Check(Force->Verb != EForceVerb::Retreat && Force->Status != EForceStatus::Retreating
					&& Force->Status != EForceStatus::Withdrawing && Force->RetreatThreshold == ERetreatThreshold::Never
					&& Plan && Plan->Verb != EForceVerb::Retreat,
				TEXT("A badly hurt free wave force fights on: no retreat, no withdrawal, no recovery"));
		}
		return true;
	}

	bool Wound()
	{
		const TArray<AArmyGroup*> Forces = EnemyForces(Kit.World);
		if (!Check(!Forces.IsEmpty() && Attacker, TEXT("The wave and a human attacker exist")))
			return true;
		for (const AArmyGroup* Force : Forces)
			for (AArmyUnit* Unit : Force->GetUnits())
			{
				Unit->ReceiveAttack(Unit->MaxHealth() * 8 / 10, Attacker);
				if (!Check(Unit->IsAlive() && Unit->GetHealth() * 100 < Unit->MaxHealth() * 35,
						TEXT("Every wave unit is below 35% health")))
					return true;
			}
		Enter(3);
		return false;
	}

	AArmyUnit* Attacker = nullptr;
};
}

bool FJevReleaseLostTargetTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FLostTargetScenario(this));
	return true;
}

bool FJevReleaseFightsOnWorldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FFightsOnScenario(this));
	return true;
}

#endif
