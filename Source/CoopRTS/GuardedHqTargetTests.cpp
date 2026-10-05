#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CombatTarget.h"
#include "Content/UnitDefinition.h"
#include "GuardedHqFixture.h"

// Guarded HQs on the real authority, second half: JEV's emergency wave, the targets nodes and HQs present, and
// a real force choosing a node over the immune HQ.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedJevEmergencyTest, "CoopRTS.Guarded.JevEmergency",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedJevNodeAttackTest, "CoopRTS.Guarded.JevNodeAttack",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedTargetsTest, "CoopRTS.Guarded.Targets",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedAcquisitionTest, "CoopRTS.Guarded.Acquisition",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

using namespace GuardedHqWorld;

namespace
{
// JEV's free units, and how many of them stand in the HQ's region.
void CountEmergencyUnits(FTeamEconomyFixture& F, const AHeadquarters& Home, int32& Units, int32& AtHome)
{
	for (TActorIterator<AArmyGroup> It(F.World); It; ++It)
		if (It->GetTeamIndex() == 5 && !It->GetProductionBuilding())
			for (const AArmyUnit* Unit : It->GetUnits())
			{
				++Units;
				AtHome += F.State->FindRegionAt(Unit->GetActorLocation()) == F.State->FindRegionAt(Home.GetActorLocation()) ? 1 : 0;
			}
}
}

bool FGuardedJevEmergencyTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		const int32 Since = Mark(F);
		AEnemyCommander* Jev = F.World->SpawnActor<AEnemyCommander>();
		if (!Jev)
		{
			T.AddError(TEXT("JEV commander unavailable"));
			return;
		}
		// Evaluations are driven by hand so nothing but the requested wave can appear.
		Jev->SetActorTickEnabled(false);
		const auto FreeForces = [&F]() {
			int32 Count = 0;
			for (TActorIterator<AArmyGroup> It(F.World); It; ++It)
				Count += It->GetTeamIndex() == 5 && !It->GetProductionBuilding() && It->GetAliveCount() > 0 ? 1 : 0;
			return Count;
		};
		AHeadquarters& Home = Hq(F, 5);
		AArmyUnit* Striker1 = Striker(F, 5);
		T.TestEqual(TEXT("No JEV force before the HQ goes offline"), FreeForces(), 0);
		Home.ReceiveAttack(100000, Striker1);
		T.TestTrue(TEXT("The Lattice HQ is offline"), Home.IsOffline());
		T.TestEqual(TEXT("The emergency is announced to all"), Events(F, TEXT("enemy_emergency"), Since), 1);
		Jev->EvaluatePlan();
		T.TestTrue(TEXT("JEV launches its emergency wave"), FreeForces() >= 1);
		int32 Units = 0;
		int32 AtHome = 0;
		CountEmergencyUnits(F, Home, Units, AtHome);
		T.TestTrue(TEXT("which has units"), Units > 0);
		T.TestEqual(TEXT("spawned at the HQ, in its main"), AtHome, Units);
		// Decision (review ruling): an emergency wave is published on the timeline as one, and is not a release wave.
		T.TestTrue(TEXT("It is published as an emergency wave"), !Jev->Release.Waves.IsEmpty() && Jev->Release.Waves.Last().bEmergency);
		T.TestEqual(TEXT("and is no release wave"), Jev->Release.WaveCount, 0);
		T.TestEqual(TEXT("It buys the v1.1 budget before 120 s, for two commanders"), Jev->Release.Waves.Last().Budget,
			JevRelease::WaveBudget(1, 2));
		T.TestEqual(TEXT("and it is free: JEV's wallet is untouched"), F.State->EnemyCommander->Resources, 0);

		// Back online and offline again: once per battle.
		ClearGroups(F, 5);
		Stand(F, 0, Main(5));
		Home.Tick(5.f);
		ClearGroups(F, 0);
		Home.Tick(5.f);
		T.TestTrue(TEXT("The HQ came back"), Home.IsOnline());
		Striker1 = Striker(F, 5);
		Home.ReceiveAttack(100000, Striker1);
		T.TestTrue(TEXT("and went offline again"), Home.IsOffline());
		Jev->EvaluatePlan();
		T.TestEqual(TEXT("with no second emergency wave"), FreeForces(), 0);
		T.TestEqual(TEXT("and no second announcement"), Events(F, TEXT("enemy_emergency"), Since), 1);
	}));
	return true;
}

bool FGuardedJevNodeAttackTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		AEnemyCommander* Jev = F.World->SpawnActor<AEnemyCommander>();
		// A human node out in the neck, a region JEV does not hold and the human main does not contain.
		const FTransform Transform(F.State->GetRegionAnchor(F.Neck) + FVector(0.f, 0.f, 150.f));
		AFailoverNode* Node = F.World->SpawnActorDeferred<AFailoverNode>(AFailoverNode::StaticClass(), Transform, nullptr,
			nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		AArmyUnit* Scout = F.SpawnHostileIn(1);
		if (!Jev || !Node || !Scout || !Scout->GetGroup())
		{
			T.AddError(TEXT("Node attack fixtures unavailable"));
			return;
		}
		Node->TeamIndex = 0;
		Node->FinishSpawning(Transform);
		Jev->SetActorTickEnabled(false);
		AArmyGroup* Force = Scout->GetGroup();
		Jev->EvaluatePlan();
		T.TestEqual(TEXT("JEV's planner sends its force to Attack"), static_cast<int32>(Force->Verb), static_cast<int32>(EForceVerb::Attack));
		T.TestTrue(TEXT("with the human node as the structure target, through the Attack-structure path"),
			Force->TargetStructure == Node && CombatTarget::IsAliveHostile(Node, 5));
	}));
	return true;
}

bool FGuardedTargetsTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		for (const int32 Team : Teams)
		{
			AFailoverNode* Node = SpawnNode(F, Team, 0);
			AHeadquarters& Home = Hq(F, Team);
			AArmyUnit* Striker1 = Striker(F, Team);
			if (!Node || !Striker1)
			{
				T.AddError(TEXT("Node fixtures unavailable"));
				return;
			}
			const int32 Attacker = Opponent(Team);
			T.TestTrue(TEXT("A node is a valid Attack target for the opposing side"), CombatTarget::IsAliveHostile(Node, Attacker));
			T.TestFalse(TEXT("and never for its own"), CombatTarget::IsAliveHostile(Node, Team));
			T.TestTrue(TEXT("An immune HQ stays a valid, allowed Attack target"), CombatTarget::IsAliveHostile(&Home, Attacker));
			T.TestEqual(TEXT("A node is a Structure"), static_cast<int32>(CombatTarget::ArmorClass(Node)),
				static_cast<int32>(EArmorClass::Structure));
			CombatTarget::ReceiveAttack(Node, 200, Striker1);
			T.TestTrue(TEXT("The shared damage path reaches a node"), Node->Health < Node->MaxHealth());
			const int32 Before = Home.Health;
			CombatTarget::ReceiveAttack(&Home, 200, Striker1);
			T.TestEqual(TEXT("and an immune HQ takes nothing through it"), Home.Health, Before);
			Node->ReceiveAttack(100000, Striker1);
			T.TestFalse(TEXT("A dead node is no target"), CombatTarget::IsAliveHostile(Node, Attacker));
			Home.ReceiveAttack(100000, Striker1);
			T.TestFalse(TEXT("and neither is an offline HQ"), CombatTarget::IsAliveHostile(&Home, Attacker));
			Clean(F);
		}
	}));
	return true;
}

namespace
{
// A real force beside a hostile node, left to fight: it must pick the node over the immune HQ, and its siege
// shots splash onto the node beside the primary victim.
class FAcquisitionScenario : public IAutomationLatentCommand
{
public:
	explicit FAcquisitionScenario(FAutomationTestBase* InTest) : Test(InTest) {}
	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 60.)
		{
			Test->AddError(TEXT("Acquisition scenario exceeded 60 seconds"));
			return true;
		}
		if (!Fixture.IsValid())
		{
			Fixture = FTeamEconomyFixture::Create(ArmyTestSetup::World(), Test);
			if (!Fixture.IsValid())
				return false;
			if (!Fixture->Reset(1))
			{
				Test->AddError(TEXT("Fixture needs regions 2, 3 and 4 with anchors and deposits"));
				return true;
			}
			Clean(*Fixture);
			return Begin();
		}
		return Observe();
	}

private:
	bool Begin()
	{
		FTeamEconomyFixture& F = *Fixture;
		Home = &Hq(F, 5);
		AFailoverNode* Placed = SpawnNode(F, 5, 0);
		Node = Placed;
		AArmyGroup* Force = ArmyTestSetup::SpawnGroup(F.World, F.Controller, 70,
			Placed ? Placed->GetActorLocation() + FVector(-700.f, 0.f, 100.f) : FVector::ZeroVector);
		if (!Placed || !Force)
		{
			Test->AddError(TEXT("Acquisition fixtures unavailable"));
			return true;
		}
		Siege = Force->GetUnits()[4];
		HomeHealth = Home->Health;
		StartedGame = ArmyTestSetup::GameSeconds(F.World);
		return false;
	}
	bool Observe()
	{
		FTeamEconomyFixture& F = *Fixture;
		if (Node->Health == Node->MaxHealth() && ArmyTestSetup::GameSeconds(F.World) - StartedGame < 20.)
			return false;
		Test->TestTrue(TEXT("A force beside a hostile node shoots it"), Node->Health < Node->MaxHealth());
		Test->TestEqual(TEXT("and never wastes shots on the immune HQ"), Home->Health, HomeHealth);
		// Splash: the siege fires at a hostile unit next to the node and the node takes its share.
		AArmyUnit* Decoy = F.SpawnHostileIn(F.Neck);
		if (Decoy && Siege.IsValid())
		{
			Decoy->SetActorLocation(Node->GetActorLocation() + FVector(0.f, 100.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			Siege->SetActorLocation(Decoy->GetActorLocation() + FVector(-600.f, 0.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
			const int32 Before = Node->Health;
			Siege->NextAttackTime = 0.f;
			Siege->FireAt(Decoy);
			Test->TestTrue(TEXT("Artillery splash includes a node beside the victim"), Node->Health < Before);
		}
		else
			Test->AddError(TEXT("Splash fixtures unavailable"));
		Clean(F);
		return true;
	}

	FAutomationTestBase* Test;
	double Started = FPlatformTime::Seconds();
	double StartedGame = 0.;
	TUniquePtr<FTeamEconomyFixture> Fixture;
	TWeakObjectPtr<AHeadquarters> Home;
	TWeakObjectPtr<AFailoverNode> Node;
	TWeakObjectPtr<AArmyUnit> Siege;
	int32 HomeHealth = 0;
};
}

bool FGuardedAcquisitionTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FAcquisitionScenario(this));
	return true;
}

#endif