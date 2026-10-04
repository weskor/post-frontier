#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "GuardedHqFixture.h"
#include "GuardedHqTestSupport.h"
#include "MatchSimulationJson.h"
#include "MatchSimulationSubsystem.h"
#include "SimulationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"

// The autopilot simulation decides and reports a battle by the HQ lifecycle, for both sides: an HQ at 0 HP is
// offline and the match goes on (an offline HQ at the time cap is a censored draw); only a completed hold ends
// it, with a `hold_completed` event, and every team snapshot carries `hq_state`.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedSimulationTest, "CoopRTS.Guarded.Simulation",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// The simulation's private observation, outcome and event log, for a recorder that is never started.
struct FMatchSimulationTestAccess
{
	// Never ticks: a started simulation would take over the world.
	static TUniquePtr<FMatchSimulation> Create(UWorld* World)
	{
		TUniquePtr<FMatchSimulation> Sim = MakeUnique<FMatchSimulation>(World);
		Sim->bFinished = true;
		return Sim;
	}
	static void Observe(FMatchSimulation& Sim, ACommandGameState& State) { Sim.ObserveHeadquarters(State); }
	static FMatchSimulation::FOutcome Resolve(const FMatchSimulation& Sim, const ACommandGameState& State, double Time)
	{
		return Sim.ResolveOutcome(State, Time);
	}
	static TArray<TSharedPtr<FJsonObject>> Events(const FMatchSimulation& Sim, const TCHAR* Kind)
	{
		TArray<TSharedPtr<FJsonObject>> Rows;
		for (const TSharedPtr<FJsonValue>& Value : Sim.Report->GetArrayField(TEXT("events")))
			if (const TSharedPtr<FJsonObject> Row = Value->AsObject(); Row.IsValid() && Row->GetStringField(TEXT("kind")) == Kind)
				Rows.Add(Row);
		return Rows;
	}
};

using namespace GuardedHqWorld;

namespace
{
FString StateOf(const AHeadquarters& Home)
{
	FJsonObject Row;
	MatchSimulationJson::HqFields(Row, Home);
	return Row.GetStringField(TEXT("hq_state"));
}
}

bool FGuardedSimulationTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FGuardedScenario(this, 1, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		const TUniquePtr<FMatchSimulation> Sim = FMatchSimulationTestAccess::Create(F.World);
		const double Cap = FSimulationSettings::Get().TimeCap;
		for (const int32 Team : Teams)
		{
			AHeadquarters& Home = Hq(F, Team);
			const int32 Rival = Opponent(Team);
			AArmyUnit* Striker1 = Striker(F, Team);
			if (!Striker1)
			{
				T.AddError(TEXT("Striker unavailable"));
				return;
			}
			T.TestEqual(TEXT("A fresh HQ reports online"), StateOf(Home), FString(TEXT("online")));
			const int32 Before = FMatchSimulationTestAccess::Events(*Sim, TEXT("hold_completed")).Num();

			Home.ReceiveAttack(100000, Striker1);
			ClearGroups(F, Team);
			FMatchSimulationTestAccess::Observe(*Sim, *F.State);
			T.TestEqual(TEXT("An HQ at 0 HP reports offline, not lost"), StateOf(Home), FString(TEXT("offline")));
			T.TestEqual(TEXT("and has completed no hold"), FMatchSimulationTestAccess::Events(*Sim, TEXT("hold_completed")).Num(), Before);
			T.TestFalse(TEXT("The battle goes on before the cap"), FMatchSimulationTestAccess::Resolve(*Sim, *F.State, 1.).bEnded);
			const auto Capped = FMatchSimulationTestAccess::Resolve(*Sim, *F.State, Cap);
			T.TestTrue(TEXT("An offline HQ at the cap ends the match"), Capped.bEnded && Capped.Error == nullptr);
			T.TestEqual(TEXT("as a censored draw, not a destroyed HQ"), FString(Capped.Kind), FString(TEXT("time_cap")));
			T.TestEqual(TEXT("with no winner"), Capped.Winner, -1);

			T.TestTrue(TEXT("The attackers complete the hold"), GuardedHqTest::CompleteHold(Home, Striker1));
			FMatchSimulationTestAccess::Observe(*Sim, *F.State);
			FMatchSimulationTestAccess::Observe(*Sim, *F.State);
			T.TestEqual(TEXT("A lost HQ reports lost"), StateOf(Home), FString(TEXT("lost")));
			const TArray<TSharedPtr<FJsonObject>> Holds = FMatchSimulationTestAccess::Events(*Sim, TEXT("hold_completed"));
			T.TestEqual(TEXT("A completed hold emits hold_completed once"), Holds.Num(), Before + 1);
			if (Holds.Num() == Before + 1)
			{
				T.TestEqual(TEXT("for the side whose HQ was lost"), static_cast<int32>(Holds.Last()->GetNumberField(TEXT("team"))), Team);
				T.TestEqual(TEXT("with the other side as the winner"), static_cast<int32>(Holds.Last()->GetNumberField(TEXT("winner"))), Rival);
			}

			// Before the game mode's tick, a hold completed in the cap's own frame is an outcome, not a draw.
			const auto Same = FMatchSimulationTestAccess::Resolve(*Sim, *F.State, Cap);
			T.TestTrue(TEXT("A hold completed in the cap's frame is decisive"), Same.bEnded && FString(Same.Kind) == TEXT("hold_completed") && Same.Winner == Rival);
			F.State->MatchResult = Team == 0 ? EMatchResult::Defeat : EMatchResult::Victory;
			const auto Result = FMatchSimulationTestAccess::Resolve(*Sim, *F.State, 1.);
			T.TestTrue(TEXT("The game's result for a lost HQ is hold_completed"), Result.bEnded && Result.Error == nullptr && FString(Result.Kind) == TEXT("hold_completed"));
			T.TestEqual(TEXT("won by the other side"), Result.Winner, Rival);
			F.State->MatchResult = Team == 0 ? EMatchResult::Victory : EMatchResult::Defeat;
			T.TestNotNull(TEXT("A result that contradicts the lifecycle is an error, not an outcome"), FMatchSimulationTestAccess::Resolve(*Sim, *F.State, 1.).Error);
			F.State->MatchResult = EMatchResult::Ongoing;
			Clean(F);
			T.TestEqual(TEXT("A reset HQ reports online again"), StateOf(Home), FString(TEXT("online")));
		}
	}));
	return true;
}
#endif
