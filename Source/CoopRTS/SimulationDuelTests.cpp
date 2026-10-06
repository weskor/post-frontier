#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "ArmyTestSetup.h"
#include "MatchSimulationSubsystem.h"
#include "ArenaBounds.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "Content/MatchContent.h"
#include "EnemyCommander.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "HAL/IConsoleManager.h"
#include "Misc/App.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FSimulationDuelTest, "CoopRTS.Simulation.Duel",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

class FSimulationDuelScenario : public IAutomationLatentCommand
{
public:
	explicit FSimulationDuelScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	~FSimulationDuelScenario() override
	{
		FApp::SetUseFixedTimeStep(bOriginalFixedStep);
		FApp::SetFixedDeltaTime(OriginalDelta);
		if (MaxFPS)
			MaxFPS->Set(OriginalMaxFPS, ECVF_SetByCode);
	}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 180.)
		{
			Test->AddError(TEXT("Duel world scenario timed out"));
			return true;
		}
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetNetMode() == NM_Standalone)
			{
				World = Context.World();
				break;
			}
		if (!World || World->GetTimeSeconds() < 3.f || (!Runner && !ArmyTestSetup::NavigationReady(World)))
			return false;
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		if (!State || !IsValid(State->Content) || !IsValid(State->Arena) || State->Regions.IsEmpty())
			return false;
		if (!Runner)
			return StartEncounter(World, State);
		const float Cap = Phase == EPhase::ShortCap ? 1.f / 60.f : Phase == EPhase::NaturalMatrix ? 25.f
			: Phase == EPhase::PausedEncounter                                                    ? 3.f * ExpectedStallTimeout + 60.f
																								  : 60.f;
		const float Delta = World->GetDeltaSeconds();
		Runner->Tick(Delta, Cap);
		const TSharedRef<FJsonObject> Report = Runner->GetReport();
		if (Report->HasField(TEXT("current_duel")))
			if (const TSharedPtr<FJsonObject> Pair = Report->GetObjectField(TEXT("current_duel")); Pair != CheckedPair)
			{
				CheckedPair = Pair;
				CheckSideTickOrder(*World, *Pair);
			}
		if (Phase == EPhase::PausedEncounter)
			return CheckPausedEncounter(*World, Report, Delta);
		if (!Runner->GetError().IsEmpty())
		{
			Test->AddError(Runner->GetError());
			return true;
		}
		if (!Runner->IsComplete())
			return false;
		const TArray<TSharedPtr<FJsonValue>>& Definitions = Report->GetArrayField(TEXT("unit_definitions"));
		const TArray<TSharedPtr<FJsonValue>>& Duels = Report->GetArrayField(TEXT("duels"));
		Test->TestEqual(TEXT("Successful matrix is complete"), Report->GetStringField(TEXT("status")), FString(TEXT("complete")));
		Test->TestEqual(TEXT("Every ordered pair including mirrors is reported"), Duels.Num(), Definitions.Num() * Definitions.Num());
		Test->TestFalse(TEXT("Successful matrices retain no invalid fight"), Report->HasField(TEXT("invalid_duel")));
		CheckCompletedRows(Report, Cap, Phase == EPhase::ShortCap ? 2 : 1);
		AdvancePhase();
		return false;
	}

private:
	enum class EPhase
	{
		FullMatrix,
		NaturalMatrix,
		ShortCap,
		PausedEncounter
	};

	bool StartEncounter(UWorld* World, ACommandGameState* State)
	{
		if (Phase == EPhase::FullMatrix)
		{
			MaxFPS = IConsoleManager::Get().FindConsoleVariable(TEXT("t.MaxFPS"));
			if (MaxFPS)
			{
				OriginalMaxFPS = MaxFPS->GetFloat();
				MaxFPS->Set(0.f, ECVF_SetByCode);
			}
			FApp::SetFixedDeltaTime(1. / 60.);
			FApp::SetUseFixedTimeStep(true);
		}
		Runner = MakeUnique<FSimulationDuelRunner>();
		const int32 Seed = Phase == EPhase::ShortCap ? 2 : 1;
		if (!Runner->Start(*State, Seed))
		{
			Test->AddError(Runner->GetError());
			return true;
		}
		int32 Living[2] = {};
		for (TActorIterator<AArmyUnit> It(World); It; ++It)
			if (It->IsAlive())
				++Living[It->GetTeamIndex() == 0 ? 0 : 1];
		Test->TestTrue(TEXT("Both opposed squads actually spawn"), Living[0] > 0 && Living[1] > 0);
		const TArray<TSharedPtr<FJsonValue>>& Initial = Runner->GetReport()->GetObjectField(TEXT("current_duel"))->GetArrayField(TEXT("initial_units"));
		Test->TestEqual(TEXT("Left telemetry counts actual spawned units"), static_cast<int32>(Initial[0]->AsNumber()), Living[0]);
		Test->TestEqual(TEXT("Right telemetry counts actual spawned units"), static_cast<int32>(Initial[1]->AsNumber()), Living[1]);
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			Test->TestTrue(TEXT("Both squads engage on Attack"), It->Order == EArmyOrder::Attack);
		if (Phase == EPhase::PausedEncounter)
		{
			// Hold approach longer than the stall timeout, then allow real contact.
			SetCombatTicks(*World, false);
			const TSharedPtr<FJsonObject> First = Runner->GetReport()->GetObjectField(TEXT("current_duel"));
			const TArray<TSharedPtr<FJsonValue>>& Definitions = Runner->GetReport()->GetArrayField(TEXT("unit_definitions"));
			ExpectedStallTimeout = FMath::Max(30., 10. * Definitions[0]->AsObject()->GetNumberField(TEXT("attack_interval")));
			Test->TestEqual(TEXT("Stall timeout accounts for the current pair's attack interval"),
				First->GetNumberField(TEXT("stall_timeout_seconds")), ExpectedStallTimeout);
		}
		if (TActorIterator<AEnemyCommander> It(World); It)
		{
			Test->AddError(TEXT("JEV remains active in the duel"));
			return true;
		}
		for (TActorIterator<ACommandBuilding> It(World); It; ++It)
			Test->TestFalse(TEXT("No paid production in duel"), It->bProductionEnabled);
		// Do not charge the encounter for the frame before its actors spawned.
		return false;
	}

	void AdvancePhase()
	{
		Runner.Reset();
		Phase = Phase == EPhase::FullMatrix  ? EPhase::NaturalMatrix
			: Phase == EPhase::NaturalMatrix ? EPhase::ShortCap
											 : EPhase::PausedEncounter;
	}

	static void SetCombatTicks(UWorld& World, bool bEnabled)
	{
		for (TActorIterator<AArmyGroup> It(&World); It; ++It)
			It->SetActorTickEnabled(bEnabled);
		for (TActorIterator<AArmyUnit> It(&World); It; ++It)
			It->SetActorTickEnabled(bEnabled);
	}

	bool CheckPausedEncounter(UWorld& World, const TSharedRef<FJsonObject>& Report, float Delta)
	{
		if (!bContactPaused)
			return CheckBeforeContact(World, Report, Delta);
		PausedElapsed += Delta;
		if (PausedElapsed < ExpectedStallTimeout)
		{
			Test->TestTrue(TEXT("Contacted encounter remains valid strictly before the stall threshold"), Runner->GetError().IsEmpty());
			Test->TestFalse(TEXT("Contacted encounter cannot complete before the threshold"), Runner->IsComplete());
			return false;
		}
		if (Runner->GetError().IsEmpty())
		{
			Test->AddError(TEXT("Paused combat was not rejected on the first tick reaching the threshold"));
			return true;
		}
		CheckInvalidStall(Report);
		const TSharedPtr<FJsonObject> Invalid = Report->GetObjectField(TEXT("invalid_duel"));
		Test->TestTrue(TEXT("Stall threshold is neither early nor delayed by another tick"),
			Invalid->GetNumberField(TEXT("no_damage_seconds")) >= ExpectedStallTimeout
				&& Invalid->GetNumberField(TEXT("no_damage_seconds")) < ExpectedStallTimeout + Delta);
		Test->TestEqual(TEXT("Silence excludes time before first contact"),
			Invalid->GetNumberField(TEXT("no_damage_seconds")), Invalid->GetNumberField(TEXT("duration")) - ContactElapsed);
		for (int32 Side = 0; Side < 2; ++Side)
		{
			Test->TestEqual(TEXT("Paused combat removes no further HP"),
				Invalid->GetArrayField(TEXT("damage_dealt"))[Side]->AsNumber(), ContactDamage[Side]);
			Test->TestEqual(TEXT("Paused sides retain every spawned member"),
				Invalid->GetArrayField(TEXT("survivors"))[Side]->AsNumber(),
				Invalid->GetArrayField(TEXT("initial_units"))[Side]->AsNumber());
		}
		Test->TestEqual(TEXT("Invalid encounter is never appended as a completed duel"),
			Report->GetArrayField(TEXT("duels")).Num(), 0);
		return true;
	}

	bool CheckBeforeContact(UWorld& World, const TSharedRef<FJsonObject>& Report, float Delta)
	{
		if (!Runner->GetError().IsEmpty() || Runner->IsComplete())
		{
			Test->AddError(TEXT("Encounter terminated before the post-contact stall fixture"));
			return true;
		}
		const TSharedPtr<FJsonObject> Current = Report->GetObjectField(TEXT("current_duel"));
		if (!bCombatResumed)
		{
			PausedElapsed += Delta;
			Test->TestEqual(TEXT("Approach time does not start the no-damage clock"),
				Current->GetNumberField(TEXT("no_damage_seconds")), 0.);
			if (PausedElapsed >= ExpectedStallTimeout + 1.)
			{
				SetCombatTicks(World, true);
				bCombatResumed = true;
			}
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>& Attacks = Current->GetArrayField(TEXT("attacks"));
		if (Attacks[0]->AsNumber() + Attacks[1]->AsNumber() == 0.)
			return false;
		ContactElapsed = Current->GetNumberField(TEXT("duration"));
		for (int32 Side = 0; Side < 2; ++Side)
			ContactDamage[Side] = Current->GetArrayField(TEXT("damage_dealt"))[Side]->AsNumber();
		Test->TestEqual(TEXT("First attack arms the clock without charging approach time"),
			Current->GetNumberField(TEXT("no_damage_seconds")), 0.);
		SetCombatTicks(World, false);
		bContactPaused = true;
		PausedElapsed = 0.;
		return false;
	}

	void CheckInvalidStall(const TSharedRef<FJsonObject>& Report)
	{
		Test->TestFalse(TEXT("An invalid stalled matrix is not complete"), Runner->IsComplete());
		Test->TestFalse(TEXT("Invalid stall retains a runner error"), Runner->GetError().IsEmpty());
		Test->TestEqual(TEXT("Stalled matrix fails"), Report->GetStringField(TEXT("status")), FString(TEXT("failed")));
		Test->TestEqual(TEXT("Stall is invalid rather than a draw"), Report->GetStringField(TEXT("outcome")), FString(TEXT("stalled")));
		Test->TestTrue(TEXT("Invalid matrix has no winner"), Report->GetField<EJson::Null>(TEXT("winner")).IsValid());
		Test->TestFalse(TEXT("Invalid encounter is no longer an active duel"), Report->HasField(TEXT("current_duel")));
		const TSharedPtr<FJsonObject> Invalid = Report->GetObjectField(TEXT("invalid_duel"));
		Test->TestEqual(TEXT("Invalid encounter records the stall"), Invalid->GetStringField(TEXT("outcome")), FString(TEXT("stalled")));
		Test->TestTrue(TEXT("Invalid encounter has no winner"), Invalid->GetField<EJson::Null>(TEXT("winner")).IsValid());
		const TArray<TSharedPtr<FJsonValue>>& Survivors = Invalid->GetArrayField(TEXT("survivors"));
		Test->TestTrue(TEXT("Stall retains living members on both sides"), Survivors[0]->AsNumber() > 0. && Survivors[1]->AsNumber() > 0.);
		Test->TestTrue(TEXT("Stall telemetry proves the no-damage timeout"),
			Invalid->GetNumberField(TEXT("no_damage_seconds")) >= Invalid->GetNumberField(TEXT("stall_timeout_seconds")));
		Test->TestTrue(TEXT("Failure report describes the invalid stall"), Report->GetStringField(TEXT("error")).Contains(TEXT("Invalid stalled duel")));
	}

	void CheckCompletedRows(const TSharedRef<FJsonObject>& Report, float Cap, int32 Seed)
	{
		const TArray<TSharedPtr<FJsonValue>>& Duels = Report->GetArrayField(TEXT("duels"));
		int32 Wipes = 0;
		double Attacks = 0., Damage = 0.;
		for (const TSharedPtr<FJsonValue>& Value : Duels)
		{
			const TSharedPtr<FJsonObject> Duel = Value->AsObject();
			Test->TestTrue(TEXT("No stalled fight is admitted into completed rows"), Duel->GetStringField(TEXT("outcome")) != TEXT("stalled"));
			Test->TestEqual(TEXT("Every pair balances creation order by seed parity"),
				static_cast<int32>(Duel->GetNumberField(TEXT("spawn_first_team"))), Seed % 2 == 0 ? 5 : 0);
			const bool bWiped = Duel->GetStringField(TEXT("outcome")) == TEXT("wiped");
			Wipes += bWiped;
			const TArray<TSharedPtr<FJsonValue>>& Survivors = Duel->GetArrayField(TEXT("survivors"));
			const TArray<TSharedPtr<FJsonValue>>& Spent = Duel->GetArrayField(TEXT("spent"));
			TArray<FVector> SpawnPositions;
			for (const TCHAR* Field : { TEXT("spawn_positions_left"), TEXT("spawn_positions_right") })
				for (const TSharedPtr<FJsonValue>& Position : Duel->GetArrayField(Field))
				{
					const TArray<TSharedPtr<FJsonValue>>& Coordinates = Position->AsArray();
					SpawnPositions.Emplace(Coordinates[0]->AsNumber(), Coordinates[1]->AsNumber(), Coordinates[2]->AsNumber());
				}
			for (int32 A = 0; A < SpawnPositions.Num(); ++A)
				for (int32 B = A + 1; B < SpawnPositions.Num(); ++B)
					Test->TestTrue(TEXT("Seeded jitter preserves collision-safe member separation"),
						FVector::DistSquared2D(SpawnPositions[A], SpawnPositions[B]) >= FMath::Square(80.f) - .1f);
			Test->TestTrue(TEXT("Whole-unit squads respect each 120-Power budget"),
				Spent[0]->AsNumber() > 0. && Spent[0]->AsNumber() <= 120.
					&& Spent[1]->AsNumber() > 0. && Spent[1]->AsNumber() <= 120.);
			if (bWiped)
				Test->TestTrue(TEXT("Wipe termination requires an empty side"), Survivors[0]->AsNumber() == 0. || Survivors[1]->AsNumber() == 0.);
			else
			{
				Test->TestEqual(TEXT("Non-wipe termination is the cap"), Duel->GetStringField(TEXT("outcome")), FString(TEXT("time_cap")));
				Test->TestTrue(TEXT("Cap draw retains both sides"), Survivors[0]->AsNumber() > 0. && Survivors[1]->AsNumber() > 0.);
				Test->TestTrue(TEXT("A valid cap draw has not already reached the stall timeout"),
					Duel->GetNumberField(TEXT("no_damage_seconds")) < Duel->GetNumberField(TEXT("stall_timeout_seconds")));
				Test->TestTrue(TEXT("Time cap is observed rather than fabricated"), Duel->GetNumberField(TEXT("duration")) >= Cap);
			}
			for (const TSharedPtr<FJsonValue>& Count : Duel->GetArrayField(TEXT("attacks")))
				Attacks += Count->AsNumber();
			for (const TSharedPtr<FJsonValue>& Amount : Duel->GetArrayField(TEXT("damage_dealt")))
				Damage += Amount->AsNumber();
		}
		if (Phase == EPhase::NaturalMatrix)
		{
			Test->TestTrue(TEXT("Real attacks and landed damage are reported"), Attacks > 0. && Damage > 0.);
			Test->TestTrue(TEXT("At least one live fight terminates naturally on a wipe"), Wipes > 0);
		}
		if (Phase == EPhase::ShortCap)
			Test->TestEqual(TEXT("Short-cap matrix does not fabricate wipes"), Wipes, 0);
	}

	// Combat resolves group by group inside a frame, so the side created first by seed parity must also tick first,
	// in every fight of the matrix, not only the first one.
	void CheckSideTickOrder(UWorld& World, const FJsonObject& Pair)
	{
		const int32 FirstTeam = static_cast<int32>(Pair.GetNumberField(TEXT("spawn_first_team")));
		TArray<const AActor*> First, Second;
		for (TActorIterator<AArmyUnit> It(&World); It; ++It)
			(It->GetTeamIndex() == FirstTeam ? First : Second).Add(*It);
		for (TActorIterator<AArmyGroup> It(&World); It; ++It)
			(It->GetTeamIndex() == FirstTeam ? First : Second).Add(*It);
		Test->TestTrue(TEXT("Both sides have actors whose tick order is checked"), !First.IsEmpty() && !Second.IsEmpty());
		for (const AActor* Later : Second)
			for (const AActor* Earlier : First)
			{
				const bool bOrdered = Later->PrimaryActorTick.GetPrerequisites().ContainsByPredicate(
					[Earlier](const FTickPrerequisite& Prerequisite) { return Prerequisite.PrerequisiteTickFunction == &Earlier->PrimaryActorTick; });
				if (!bOrdered)
				{
					Test->AddError(TEXT("A second-created duel actor can tick before a first-created one"));
					return;
				}
			}
	}

	FAutomationTestBase* Test;
	double Started;
	EPhase Phase = EPhase::FullMatrix;
	double PausedElapsed = 0.;
	double ExpectedStallTimeout = 0.;
	double ContactElapsed = 0.;
	double ContactDamage[2] = {};
	bool bCombatResumed = false;
	bool bContactPaused = false;
	const bool bOriginalFixedStep = FApp::UseFixedTimeStep();
	const double OriginalDelta = FApp::GetFixedDeltaTime();
	IConsoleVariable* MaxFPS = nullptr;
	float OriginalMaxFPS = 0.f;
	TUniquePtr<FSimulationDuelRunner> Runner;
	TSharedPtr<FJsonObject> CheckedPair;
};

bool FSimulationDuelTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSimulationDuelScenario(this));
	return true;
}

#endif
