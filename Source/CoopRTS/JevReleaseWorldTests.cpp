#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "JevReleaseWorldFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleaseWavesTest, "CoopRTS.Enemy.Release.Waves",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FJevReleasePauseTest, "CoopRTS.Enemy.Release.Pause",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace JevWorldKit;

class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest) {}

	bool Update() override
	{
		if (bFailed)
			return true;
		if (FPlatformTime::Seconds() - StartedReal > 120.)
			return Fail(TEXT("Release scenario exceeded its real-time bound"));
		if (!bReady)
		{
			if (!Acquire(Kit))
				return false;
			bReady = true;
			if (!Quarantine(Kit, 0))
				return Fail(TEXT("The isolated JEV planner could not spawn"));
			return Prepare();
		}
		if (Kit.State->MatchResult != EMatchResult::Ongoing && !bMatchMayEnd)
			return Fail(TEXT("The match ended during a release scenario"));
		return Step();
	}

protected:
	virtual bool Prepare() = 0;
	virtual bool Step() = 0;

	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		bFailed = true;
		return true;
	}
	bool Check(bool bOk, const TCHAR* Message)
	{
		if (!bOk)
			Test->AddError(Message);
		return bOk;
	}
	void SkipTo(float Seconds) { Kit.Planner->SkipClock(Seconds - Kit.Planner->GetMatchSeconds()); }
	void Enter(int32 Next)
	{
		Stage = Next;
		StageStarted = ArmyTestSetup::GameSeconds(Kit.World);
	}
	double InStage() const { return ArmyTestSetup::GameSeconds(Kit.World) - StageStarted; }
	const FJevReleaseState& Release() const { return Kit.Planner->Release; }
	const FJevPublishedPlan* PlanOf(const AArmyGroup* Force) const
	{
		return Kit.State->EnemyPlans.FindByPredicate([Force](const FJevPublishedPlan& Plan) { return Plan.Force == Force; });
	}
	// Waits for the Count-th wave; false while it has not launched, and an error after three game seconds.
	bool WaveLaunched(int32 Count)
	{
		if (Release().WaveCount >= Count)
			return true;
		if (InStage() > 3.)
			Fail(*FString::Printf(TEXT("Wave %d did not launch within three game seconds of its release"), Count));
		return false;
	}

	FAutomationTestBase* Test;
	JevWorldKit::FKit Kit;
	int32 Stage = 0;
	double StageStarted = 0.;
	double StartedReal = FPlatformTime::Seconds();
	bool bReady = false;
	bool bFailed = false;
	bool bMatchMayEnd = false;
};

class FWaveScenario : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		const int32 Heavy = static_cast<int32>(EArmorClass::Heavy);
		// One human squad reduced to its Brawlers, so Heavy is the humans' most numerous class.
		AArmyGroup* Humans = ArmyTestSetup::SpawnGroup(Kit.World, Kit.PC, 30,
			ArmyTestSetup::FromFriendlyHQ(Kit.State, -250.f, 350.f, 100.f));
		if (!Humans)
			return Fail(TEXT("Human fixture squad could not spawn"));
		Park(*Humans);
		TArray<AArmyUnit*> Squad;
		for (AArmyUnit* Unit : Humans->GetUnits())
			Squad.Add(Unit);
		for (AArmyUnit* Unit : Squad)
			if (static_cast<int32>(Unit->GetArmorClass()) != Heavy)
			{
				Humans->OnMemberDied(Unit);
				if (AController* Controller = Unit->GetController())
					Controller->Destroy();
				Unit->Destroy();
			}
		HumanMain = ArmyTestSetup::RegionAt(Kit.State, Kit.State->FriendlyHeadquarters->GetActorLocation());
		JevMain = ArmyTestSetup::RegionAt(Kit.State, Kit.State->EnemyHeadquarters->GetActorLocation());
		Brawler = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Frontline);
		Rifle = ArmyTestSetup::UnitIndex(Kit.State, EUnitRole::Ranged);
		SkipTo(80.f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		switch (Stage)
		{
		case 1:
			if (!Check(Release().Current == 0 && Release().Next == 1, *FString::Printf(TEXT("At 80 s v1.0 is in force and v1.1 next (current %d next %d)"),
					Release().Current, Release().Next))
				|| !Check(Release().NextAt == 120.f && !Release().bNextShown,
					*FString::Printf(TEXT("v1.1 is due at 120 s and is not on the timeline at 80 s (at %.1f, shown %d, match %.1f)"),
						Release().NextAt, Release().bNextShown, Kit.Planner->GetMatchSeconds()))
				|| !Check(Release().WaveCount == 0 && EnemyForces(Kit.World).IsEmpty(),
					*FString::Printf(TEXT("No wave or JEV force before v1.1 (waves %d, forces %d)"), Release().WaveCount,
						EnemyForces(Kit.World).Num())))
				return true;
			SkipTo(100.f);
			Enter(2);
			return false;
		case 2:
			if (!Check(Release().bNextShown && Release().NextAt == 120.f && Release().WaveCount == 0,
					TEXT("30 s ahead of v1.1 the release shows on the timeline without a wave")))
				return true;
			SkipTo(119.2f);
			Enter(3);
			return false;
		case 3:
			if (!Check(Release().Current == 0 && Release().WaveCount == 0 && EnemyForces(Kit.World).IsEmpty(),
					TEXT("No wave before the release boundary")))
				return true;
			SkipTo(120.8f);
			Enter(4);
			return false;
		case 4:
			return WaveLaunched(1) ? FirstWave() : false;
		case 5:
			return Settled();
		default:
			return LaterWaves();
		}
	}

	const FJevPublishedPlan* VerifyForce(const AArmyGroup* Force, int32 Target)
	{
		const FJevPublishedPlan* Plan = PlanOf(Force);
		const bool bOk = Force->Verb == EForceVerb::Attack && Force->TargetRegionIndex == Target && !Force->Orders.IsEmpty()
			&& Plan && Plan->Verb == EForceVerb::Attack && Plan->TargetRegionIndex == Target && Plan->TicketNumber > 0
			&& Plan->CommittedUntil > Kit.World->GetTimeSeconds();
		Check(bOk, TEXT("A wave force executes Attack on its target and publishes a plan for it"));
		return Plan;
	}

	bool FirstWave()
	{
		const FJevWaveEvent& Event = Release().Waves.Last();
		const TArray<AArmyGroup*> Forces = EnemyForces(Kit.World);
		const bool bEvent = Event.Release == 1 && Event.Budget == 150 && Event.Units == 7 && Event.Forces == 2
			&& Event.TargetRegion == HumanMain;
		const bool bState = Release().Current == 1 && Release().Next == 2 && Release().NextAt == 240.f
			&& !Release().bNextShown && Release().WaveCount == 1;
		if (!Check(bEvent && bState, TEXT("v1.1 publishes one wave event: 150 Power, 7 units in 2 forces, raiding the human main")))
			return true;
		if (!Check(Forces.Num() == 2 && CountUnits(Forces, Brawler) == 7,
				TEXT("v1.1 buys the cheapest unit: seven Brawlers split into 4 and 3")))
			return true;
		if (!Check(Kit.State->EnemyCommander->Resources == 0, TEXT("The wave spawned without touching JEV's wallet")))
			return true;
		for (const AArmyGroup* Force : Forces)
		{
			if (!Check(ArmyTestSetup::CurrentRegion(Force) == JevMain, TEXT("Wave forces spawn in JEV's main")))
				return true;
			VerifyForce(Force, HumanMain);
		}
		Enter(5);
		return false;
	}

	// One game second after the boundary the wave is still the only one, then v1.2 is approached with two commanders.
	bool Settled()
	{
		if (InStage() < 1.)
			return false;
		if (!Check(Release().WaveCount == 1 && EnemyForces(Kit.World).Num() == 2,
				TEXT("A release launches its wave exactly once")))
			return true;
		Other = Kit.World->SpawnActor<ACommandPlayerState>();
		if (!Other)
			return Fail(TEXT("Second commander fixture could not spawn"));
		Other->TeamIndex = 0;
		Other->CommanderIndex = Kit.Wallet->CommanderIndex == 0 ? 1 : 0;
		Kit.State->AddPlayerState(Other);
		Known = EnemyForces(Kit.World);
		SkipTo(239.2f);
		Enter(6);
		return false;
	}

	bool LaterWaves()
	{
		switch (Stage)
		{
		case 6:
			if (!Check(Release().WaveCount == 1, TEXT("No v1.2 wave before 240 s")))
				return true;
			SkipTo(240.8f);
			Enter(7);
			return false;
		case 7:
			return WaveLaunched(2) ? SecondWave() : false;
		case 8:
			if (!Check(Release().WaveCount == 2, TEXT("No v2.0 wave before 360 s")))
				return true;
			for (const AArmyGroup* Force : EnemyForces(Kit.World))
				if (const FJevPublishedPlan* Plan = PlanOf(Force))
					Tickets.Add(Force->ForceNumber, Plan->TicketNumber);
			SkipTo(360.8f);
			Enter(9);
			return false;
		case 9:
			return WaveLaunched(3) ? ThirdWave() : false;
		default:
			return Ended();
		}
	}

	bool SecondWave()
	{
		const FJevWaveEvent& Event = Release().Waves.Last();
		TArray<AArmyGroup*> Fresh = EnemyForces(Kit.World);
		Fresh.RemoveAll([this](AArmyGroup* Force) { return Known.Contains(Force); });
		// Two commanders scale 250 to 325; the 10 Power carried from v1.1 joins it.
		if (!Check(Event.Release == 2 && Event.Budget == 335 && Event.Units == 14 && Event.Forces == 3,
				TEXT("v1.2 for two commanders buys with 325 plus the 10 carried: 14 units in 3 forces")))
			return true;
		if (!Check(Fresh.Num() == 3 && CountUnits(Fresh, Rifle) == 13 && CountUnits(Fresh, Brawler) == 1,
				TEXT("v1.2 counters the humans' Heavy majority with Rifles and fills the rest with the cheapest unit")))
			return true;
		if (!Check(Kit.State->EnemyCommander->Resources == 0, TEXT("The second wave left JEV's wallet untouched")))
			return true;
		for (const AArmyGroup* Force : Fresh)
			VerifyForce(Force, HumanMain);
		Known = EnemyForces(Kit.World);
		SkipTo(359.2f);
		Enter(8);
		return false;
	}

	bool ThirdWave()
	{
		const FJevWaveEvent& Event = Release().Waves.Last();
		if (!Check(Event.Release == 3 && Event.Budget == 523 && Event.Units == 21 && Event.Forces == 4,
				TEXT("v2.0 buys with 520 plus the 3 carried: 21 units in 4 forces")))
			return true;
		for (const AArmyGroup* Force : EnemyForces(Kit.World))
		{
			const FJevPublishedPlan* Plan = VerifyForce(Force, HumanMain);
			const int32* Before = Tickets.Find(Force->ForceNumber);
			Check(!Before || (Plan && Plan->TicketNumber > *Before),
				TEXT("At v2.0 every JEV force already in the field takes a fresh Attack ticket with the wave"));
		}
		bMatchMayEnd = true;
		Kit.State->SetMatchResult(EMatchResult::Victory);
		SkipTo(480.8f);
		Enter(10);
		return false;
	}

	bool Ended()
	{
		if (InStage() < 1.5)
			return false;
		Check(Release().WaveCount == 3 && Release().Current == 3, TEXT("Once the match has ended no release launches"));
		return true;
	}

	int32 HumanMain = INDEX_NONE, JevMain = INDEX_NONE, Brawler = INDEX_NONE, Rifle = INDEX_NONE;
	TArray<AArmyGroup*> Known;
	TMap<int32, int32> Tickets;
	ACommandPlayerState* Other = nullptr;
};

class FPauseScenario : public FScenario
{
public:
	using FScenario::FScenario;

private:
	bool Prepare() override
	{
		SkipTo(118.5f);
		Enter(1);
		return false;
	}

	bool Step() override
	{
		switch (Stage)
		{
		case 1:
			if (!Check(Release().WaveCount == 0, TEXT("No wave at 118.5 s")))
				return true;
			if (!FCommandService::Pause(Kit.PC))
				return Fail(TEXT("Pause command rejected"));
			Frozen = Kit.Planner->GetMatchSeconds();
			PausedAt = Kit.World->GetRealTimeSeconds();
			Enter(2);
			return false;
		case 2:
			// Game time is frozen while paused, so this wait is on real time.
			if (Kit.World->GetRealTimeSeconds() - PausedAt < 1.6)
				return false;
			if (!Check(Kit.Planner->GetMatchSeconds() == Frozen && Release().WaveCount == 0 && Release().Current == 0,
					TEXT("While paused the match clock stands still and no release arrives")))
				return true;
			if (!FCommandService::Resume(Kit.PC))
				return Fail(TEXT("Resume command rejected"));
			Enter(3);
			return false;
		case 3:
			if (!WaveLaunched(1))
				return false;
			Check(Kit.Planner->GetMatchSeconds() >= 120.f && Kit.Planner->GetMatchSeconds() < 121.5f,
				TEXT("After resuming, the release lands at its boundary 1.5 match seconds later, not early"));
			return true;
		default:
			return true;
		}
	}

	float Frozen = 0.f;
	double PausedAt = 0.;
};
}

bool FJevReleaseWavesTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FWaveScenario(this));
	return true;
}

bool FJevReleasePauseTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FPauseScenario(this));
	return true;
}

#endif
