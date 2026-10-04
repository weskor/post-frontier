#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CombatTarget.h"
#include "Content/MatchContent.h"
#include "Content/UnitDefinition.h"
#include "FailoverNode.h"
#include "GameState/GameStateTerritory.h"
#include "GuardedHqTestSupport.h"
#include "ObjectiveAnnouncer.h"
#include "GuardedHqFixture.h"

// Guarded HQs on the real authority, for both sides: the nodes' immunity and plating, the offline hold and
// its revival, the emergency force once per side, and the targets nodes and HQs present. Hold time is advanced
// by ticking the HQ directly, the way FortifyWorldTests move the published clock; the rules tests pin the
// arithmetic. A completed hold is reset before the game mode can see it: the outcome itself is proved by the
// match-outcome scenarios.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedImmunityTest, "CoopRTS.Guarded.Immunity",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedBountyTest, "CoopRTS.Guarded.Bounty",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedPlatingTest, "CoopRTS.Guarded.Plating",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedHoldTest, "CoopRTS.Guarded.Hold",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedEmergencyTest, "CoopRTS.Guarded.Emergency",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

using namespace GuardedHqWorld;

bool FGuardedImmunityTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		for (const int32 Team : Teams)
		{
			AHeadquarters& Home = Hq(F, Team);
			const int32 Since = Mark(F);
			AFailoverNode* First = SpawnNode(F, Team, 0);
			AFailoverNode* Second = SpawnNode(F, Team, 1);
			AArmyUnit* Striker1 = Striker(F, Team);
			if (!First || !Second || !Striker1)
			{
				T.AddError(TEXT("Node fixtures unavailable"));
				return;
			}
			T.TestEqual(TEXT("Both nodes register with their HQ"), Home.NodesStanding(), 2);
			T.TestTrue(TEXT("and the HQ is immune"), Home.IsImmune());
			Home.ReceiveAttack(500, Striker1);
			T.TestEqual(TEXT("Two nodes standing: the HQ takes no damage"), Home.Health, Home.MaxHealth());

			// Plating is off the table here: the clock is the JEV commander's, which these scenarios destroy.
			First->ReceiveAttack(100000, Striker1);
			T.TestFalse(TEXT("A node dies at 0 HP"), First->IsAlive());
			T.TestEqual(TEXT("One node left"), Home.NodesStanding(), 1);
			T.TestEqual(TEXT("The loss is announced to all"),
				Events(F, Team == 0 ? TEXT("own_node_lost") : TEXT("enemy_node_lost"), Since), 1);
			const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(F.State);
			T.TestTrue(TEXT("with the number of nodes left"), Announcer && Announcer->GetEvents().Last().DamageTier == 1 && Announcer->GetEvents().Last().AffectedTeam == Team);
			Home.ReceiveAttack(500, Striker1);
			T.TestEqual(TEXT("Either node is enough: still no damage"), Home.Health, Home.MaxHealth());
			First->ReceiveAttack(100000, Striker1);
			T.TestEqual(TEXT("A dead node takes no more damage and announces nothing twice"),
				Events(F, Team == 0 ? TEXT("own_node_lost") : TEXT("enemy_node_lost"), Since), 1);

			Second->ReceiveAttack(100000, Striker1);
			T.TestEqual(TEXT("Both nodes dead"), Home.NodesStanding(), 0);
			T.TestFalse(TEXT("so the HQ is no longer immune"), Home.IsImmune());
			Home.ReceiveAttack(500, Striker1);
			T.TestEqual(TEXT("and takes its damage"), Home.Health, Home.MaxHealth() - 500);
			T.TestTrue(TEXT("Still online at 400 HP"), Home.IsOnline());
			Clean(F);
		}
	}));
	return true;
}

bool FGuardedBountyTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		AFailoverNode* Hostile = SpawnNode(F, 5, 0);
		AFailoverNode* Friendly = SpawnNode(F, 0, 0);
		AArmyUnit* Raider = F.SpawnHostileIn(F.Neck);
		AArmyUnit* Striker1 = F.SpawnAttacker();
		if (!Hostile || !Friendly || !Raider || !Striker1)
		{
			T.AddError(TEXT("Node fixtures unavailable"));
			return;
		}
		F.Step(); // The state notices the standing nodes.
		Friendly->ReceiveAttack(100000, Raider);
		F.Step();
		T.TestEqual(TEXT("Losing one of its own nodes pays the humans nothing"), F.Wallets[0]->Data, 0);
		Hostile->ReceiveAttack(100000, Striker1);
		F.Step();
		T.TestEqual(TEXT("A destroyed JEV node pays 60 Data"), F.Wallets[0]->Data, 60);
		F.Step();
		T.TestEqual(TEXT("once"), F.Wallets[0]->Data, 60);
	}));
	return true;
}

bool FGuardedPlatingTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		AEnemyCommander* Clock = F.World->SpawnActor<AEnemyCommander>();
		if (!Clock)
		{
			T.AddError(TEXT("JEV clock unavailable"));
			return;
		}
		// Only the clock is wanted: JEV must not plan or spawn anything in this scenario.
		Clock->SetActorTickEnabled(false);
		for (const int32 Team : Teams)
		{
			AFailoverNode* Node = SpawnNode(F, Team, 0);
			AArmyUnit* Striker1 = Striker(F, Team);
			if (!Node || !Striker1)
			{
				T.AddError(TEXT("Node fixtures unavailable"));
				return;
			}
			T.TestTrue(TEXT("A node is plated at the start of the battle"), Node->IsPlated());
			Node->ReceiveAttack(200, Striker1);
			T.TestEqual(TEXT("Plating takes 90% of the damage"), Node->MaxHealth() - Node->Health, 20);
			Clock->SkipClock(239.f - Clock->GetMatchSeconds());
			T.TestTrue(TEXT("Still plated just before v1.2 (240 s)"), Node->IsPlated());
			Node->ReceiveAttack(200, Striker1);
			T.TestEqual(TEXT("and the same 90% reduction"), Node->MaxHealth() - Node->Health, 40);
			Clock->SkipClock(2.f);
			T.TestFalse(TEXT("The plating ends at v1.2"), Node->IsPlated());
			Node->ReceiveAttack(200, Striker1);
			T.TestEqual(TEXT("and a hit then costs its full damage"), Node->MaxHealth() - Node->Health, 240);
			Clock->SkipClock(-(Clock->GetMatchSeconds() - 10.f));
			Node->Destroy();
		}
	}));
	return true;
}

bool FGuardedHoldTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		for (const int32 Team : Teams)
		{
			AHeadquarters& Home = Hq(F, Team);
			const int32 Since = Mark(F);
			const int32 Region = Main(Team);
			AArmyUnit* Striker1 = Striker(F, Team);
			if (!Striker1)
			{
				T.AddError(TEXT("Striker unavailable"));
				return;
			}
			// Without nodes the first lethal hit takes the HQ offline, not out of the battle.
			Home.ReceiveAttack(100000, Striker1);
			ClearGroups(F, Team);
			T.TestEqual(TEXT("An HQ at 0 HP"), Home.Health, 0);
			T.TestTrue(TEXT("is offline"), Home.IsOffline());
			T.TestTrue(TEXT("and still in the battle"), Home.IsAlive());
			T.TestEqual(TEXT("which is not over"), static_cast<int32>(F.State->MatchResult), static_cast<int32>(EMatchResult::Ongoing));
			T.TestFalse(TEXT("An offline HQ is not a target"), CombatTarget::IsAliveHostile(&Home, Opponent(Team)));
			const int32 Before = Home.Health;
			Home.ReceiveAttack(100, Striker1);
			T.TestEqual(TEXT("and takes no damage"), Home.Health, Before);
			T.TestEqual(TEXT("Its main stays controlled by its owner"), F.State->GetRegionController(Region), Team);
			GameStateTerritory::RefreshConnections(*F.State);
			T.TestTrue(TEXT("and connected"), (F.State->GetConnectedMask(Team) & (uint64(1) << Region)) != 0);

			// Nobody present and no progress ever: it stays offline.
			Home.Tick(30.f);
			T.TestTrue(TEXT("An empty main with no progress does not revive the HQ"), Home.IsOffline());
			T.TestEqual(TEXT("The hold reads decaying"), static_cast<int32>(Home.GetHoldState()),
				static_cast<int32>(HqHoldPolicy::EHoldState::Decaying));

			// Attackers alone advance the hold.
			AArmyUnit* Attacker = Stand(F, Opponent(Team), Region);
			Home.Tick(10.f);
			T.TestTrue(TEXT("Attackers alone advance the hold"), FMath::IsNearlyEqual(Home.GetHold().Progress, 10.f, .01f));
			T.TestEqual(TEXT("which reads holding"), static_cast<int32>(Home.GetHoldState()),
				static_cast<int32>(HqHoldPolicy::EHoldState::Holding));
			// Any defender pauses it.
			AArmyUnit* Defender = Stand(F, Team, Region);
			Home.Tick(10.f);
			T.TestTrue(TEXT("A defender pauses it"), FMath::IsNearlyEqual(Home.GetHold().Progress, 10.f, .01f));
			T.TestEqual(TEXT("which reads paused"), static_cast<int32>(Home.GetHoldState()),
				static_cast<int32>(HqHoldPolicy::EHoldState::Paused));
			Remove(Defender);
			Home.Tick(5.f);
			T.TestTrue(TEXT("Alone again, the attackers resume"), FMath::IsNearlyEqual(Home.GetHold().Progress, 15.f, .01f));
			// An empty main decays at the same rate.
			Remove(Attacker);
			Home.Tick(5.f);
			T.TestTrue(TEXT("An empty main decays at the same rate"), FMath::IsNearlyEqual(Home.GetHold().Progress, 10.f, .01f));
			T.TestEqual(TEXT("which reads decaying"), static_cast<int32>(Home.GetHoldState()),
				static_cast<int32>(HqHoldPolicy::EHoldState::Decaying));
			// Zero progress after progress existed revives the HQ at a quarter of its HP.
			Home.Tick(10.f);
			T.TestTrue(TEXT("Decay back to zero brings the HQ back online"), Home.IsOnline());
			T.TestEqual(TEXT("at 25% HP"), Home.Health, Home.MaxHealth() / 4);
			T.TestEqual(TEXT("with no hold left"), static_cast<int32>(Home.GetHoldState()),
				static_cast<int32>(HqHoldPolicy::EHoldState::None));
			T.TestEqual(TEXT("The revival is announced"),
				Events(F, Team == 0 ? TEXT("own_hq_online") : TEXT("enemy_hq_online"), Since), 1);

			// 75 s of uninterrupted attackers complete the hold and lose the HQ.
			Home.ReceiveAttack(100000, Striker1);
			ClearGroups(F, Team);
			Attacker = Stand(F, Opponent(Team), Region);
			Home.Tick(74.f);
			T.TestTrue(TEXT("74 s in, the HQ is still offline"), Home.IsOffline());
			T.TestTrue(TEXT("with 74 s of progress"), FMath::IsNearlyEqual(Home.GetHold().Progress, 74.f, .01f));
			Home.Tick(1.f);
			T.TestFalse(TEXT("75 s of uninterrupted progress complete the hold: the HQ is lost"), Home.IsAlive());
			T.TestEqual(TEXT("and an offline-only state is gone"), static_cast<int32>(Home.GetPhase()),
				static_cast<int32>(HqHoldPolicy::EPhase::Lost));
			Home.ResetForTest(Home.MaxHealth()); // Before the game mode can see a lost HQ and end the shared match.
			Clean(F);
		}
	}));
	return true;
}

bool FGuardedEmergencyTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		const int32 Since = Mark(F);
		const int32 Frontline = ArmyTestSetup::UnitIndex(F.State, EUnitRole::Frontline);
		const int32 Ranged = ArmyTestSetup::UnitIndex(F.State, EUnitRole::Ranged);
		// The second commander's Barracks makes Ranged; the first has none and takes the default Frontline.
		ACommandBuilding* Barracks = F.SpawnBarracks(0, 1.f, 0);
		if (!Barracks)
		{
			T.AddError(TEXT("Barracks fixture unavailable"));
			return;
		}
		Barracks->OwningPlayerState = F.Wallets[1];
		Barracks->bForceConfigured = true;
		Barracks->ProductionUnitIndex = Ranged;
		Barracks->ForceNumber = 1;
		AArmyUnit* Striker1 = Striker(F, 0);
		AHeadquarters& Home = Hq(F, 0);
		Home.ReceiveAttack(100000, Striker1);
		int32 Forces[2] = {};
		for (TActorIterator<AArmyGroup> It(F.World); It; ++It)
		{
			if (It->GetTeamIndex() != 0)
				continue;
			const int32 Slot = F.Wallets.IndexOfByKey(It->GetOwningPlayerState());
			if (Slot == INDEX_NONE)
				continue;
			++Forces[Slot];
			const int32 Expected = Slot == 1 ? Ranged : Frontline;
			bool bFull = It->GetAliveCount() == F.State->Content->Unit(Expected)->Capacity;
			bool bType = true;
			bool bAtHome = true;
			for (const AArmyUnit* Unit : It->GetUnits())
			{
				bType &= Unit->GetUnitIndex() == Expected;
				bAtHome &= F.State->FindRegionAt(Unit->GetActorLocation()) == F.State->FindRegionAt(Home.GetActorLocation());
			}
			T.TestTrue(*FString::Printf(TEXT("Commander %d's emergency force is a full squad of its Barracks unit type"), Slot + 1), bFull && bType);
			T.TestTrue(TEXT("spawned at the HQ, in the main"), bAtHome);
		}
		T.TestTrue(TEXT("Every commander gets exactly one free force"), Forces[0] == 1 && Forces[1] == 1);
		T.TestEqual(TEXT("The emergency is announced to all"), Events(F, TEXT("own_emergency"), Since), 1);
		T.TestEqual(TEXT("without touching a wallet"), F.Wallets[0]->Resources + F.Wallets[1]->Resources, 0);

		// Back online and offline again: the emergency is once per battle per side.
		ClearGroups(F, 0);
		Stand(F, 5, Main(0));
		Home.Tick(5.f);
		Remove(Striker1);
		ClearGroups(F, 5);
		Home.Tick(5.f);
		T.TestTrue(TEXT("The HQ came back"), Home.IsOnline());
		Striker1 = Striker(F, 0);
		Home.ReceiveAttack(100000, Striker1);
		T.TestTrue(TEXT("and went offline a second time"), Home.IsOffline());
		int32 Again = 0;
		for (TActorIterator<AArmyGroup> It(F.World); It; ++It)
			Again += It->GetTeamIndex() == 0 ? 1 : 0;
		T.TestEqual(TEXT("with no second emergency force"), Again, 0);
		T.TestEqual(TEXT("and no second announcement"), Events(F, TEXT("own_emergency"), Since), 1);
	}));
	return true;
}

#endif
