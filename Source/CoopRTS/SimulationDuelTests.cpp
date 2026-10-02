#if WITH_DEV_AUTOMATION_TESTS

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
		if (FPlatformTime::Seconds() - Started > 120.)
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
		if (!World || World->GetTimeSeconds() < 3.f)
			return false;
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		if (!State || !IsValid(State->Content) || !IsValid(State->Arena) || State->Regions.IsEmpty())
			return false;
		if (!Runner)
		{
			if (!bCapProbe)
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
			if (!Runner->Start(*State, 7))
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
			if (TActorIterator<AEnemyCommander> It(World); It)
			{
				Test->AddError(TEXT("JEV remains active in the duel"));
				return true;
			}
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				Test->TestFalse(TEXT("No paid production in duel"), It->bProductionEnabled);
		}
		Runner->Tick(World->GetDeltaSeconds(), bCapProbe ? 1.f / 60.f : 60.f);
		if (!Runner->GetError().IsEmpty())
		{
			Test->AddError(Runner->GetError());
			return true;
		}
		if (!Runner->IsComplete())
			return false;
		const TSharedRef<FJsonObject> Report = Runner->GetReport();
		const TArray<TSharedPtr<FJsonValue>>& Definitions = Report->GetArrayField(TEXT("unit_definitions"));
		const TArray<TSharedPtr<FJsonValue>>& Duels = Report->GetArrayField(TEXT("duels"));
		Test->TestEqual(TEXT("Every ordered pair including mirrors is reported"), Duels.Num(), Definitions.Num() * Definitions.Num());
		int32 Wipes = 0;
		double Attacks = 0., Damage = 0.;
		for (const TSharedPtr<FJsonValue>& Value : Duels)
		{
			const TSharedPtr<FJsonObject> Duel = Value->AsObject();
			const bool bWiped = Duel->GetStringField(TEXT("outcome")) == TEXT("wiped");
			Wipes += bWiped;
			const TArray<TSharedPtr<FJsonValue>>& Survivors = Duel->GetArrayField(TEXT("survivors"));
			const TArray<TSharedPtr<FJsonValue>>& Spent = Duel->GetArrayField(TEXT("spent"));
			Test->TestTrue(TEXT("Whole-unit squads respect each 120-Power budget"),
				Spent[0]->AsNumber() > 0. && Spent[0]->AsNumber() <= 120.
					&& Spent[1]->AsNumber() > 0. && Spent[1]->AsNumber() <= 120.);
			if (bWiped)
				Test->TestTrue(TEXT("Wipe termination requires an empty side"), Survivors[0]->AsNumber() == 0. || Survivors[1]->AsNumber() == 0.);
			else
			{
				Test->TestEqual(TEXT("Non-wipe termination is the cap"), Duel->GetStringField(TEXT("outcome")), FString(TEXT("time_cap")));
				Test->TestTrue(TEXT("Cap draw retains both sides"), Survivors[0]->AsNumber() > 0. && Survivors[1]->AsNumber() > 0.);
				Test->TestTrue(TEXT("Time cap is observed rather than fabricated"), Duel->GetNumberField(TEXT("duration")) >= (bCapProbe ? 1. / 60. : 60.));
			}
			for (const TSharedPtr<FJsonValue>& Count : Duel->GetArrayField(TEXT("attacks")))
				Attacks += Count->AsNumber();
			for (const TSharedPtr<FJsonValue>& Amount : Duel->GetArrayField(TEXT("damage_dealt")))
				Damage += Amount->AsNumber();
		}
		if (!bCapProbe)
		{
			Test->TestTrue(TEXT("Real attacks and landed damage are reported"), Attacks > 0. && Damage > 0.);
			Test->TestTrue(TEXT("At least one live fight terminates naturally on a wipe"), Wipes > 0);
			Runner.Reset();
			bCapProbe = true;
			return false;
		}
		Test->TestEqual(TEXT("Short-cap matrix does not fabricate wipes"), Wipes, 0);
		return true;
	}

private:
	FAutomationTestBase* Test;
	double Started;
	bool bCapProbe = false;
	const bool bOriginalFixedStep = FApp::UseFixedTimeStep();
	const double OriginalDelta = FApp::GetFixedDeltaTime();
	IConsoleVariable* MaxFPS = nullptr;
	float OriginalMaxFPS = 0.f;
	TUniquePtr<FSimulationDuelRunner> Runner;
};

bool FSimulationDuelTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSimulationDuelScenario(this));
	return true;
}

#endif
