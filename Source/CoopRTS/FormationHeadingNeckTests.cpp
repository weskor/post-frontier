#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "FormationHeadingNeck.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFormationHeadingNeckTest, "CoopRTS.Forces.FormationHeading.Neck",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace FormationHeadingNeckTests
{
// A six-member force marches two or more hops through the narrowest neck the map offers on its first leg, really
// walking (no teleporting, no frozen plan). The members' extent across the route is measured when the force starts
// and while its centre passes the neck; the same measurement runs against main (it uses no formation API).
class FScenario : public IAutomationLatentCommand
{
public:
	explicit FScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 150.)
			return Fail(FString::Printf(TEXT("Neck march timed out at stage %d"), Stage));
		UWorld* World = ArmyTestSetup::World();
		State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		Controller = World ? ArmyTestSetup::Controller(World) : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !Controller.IsValid() || ArmyTestSetup::GameSeconds(World) < 3. || !ArmyTestSetup::NavigationReady(World))
			return false;
		const double Now = ArmyTestSetup::GameSeconds(World);
		if (Stage == 0)
			return Begin(*World, Now);
		if (Stage == 1)
			return Now - StageStarted >= .5 ? BuildWalls(*World, Now) : false;
		if (Stage == 2)
			return Now - StageStarted >= 2.5 ? Order(*World, Now) : false;
		return March(Now);
	}

private:
	bool Fail(const FString& Message)
	{
		Test->AddError(Message);
		return true;
	}

	bool Begin(UWorld& World, double Now)
	{
		for (TActorIterator<AEnemyCommander> It(&World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(&World); It; ++It)
			if (It->IsOpposingArmy())
				It->Destroy();
		Home = ArmyTestSetup::RegionAt(State, State->FriendlyHeadquarters->GetActorLocation());
		Force = ArmyTestSetup::SpawnGroup(&World, Controller.Get(), 0, State->GetRegionAnchor(Home) + FVector(0.f, -350.f, 100.f));
		if (!Force.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("A six-member fixture force must spawn"));
		Stage = 1;
		StageStarted = Now;
		return false;
	}

	// The map's first legs run through open ground (no wall within 7 m of any of them), so the neck is built:
	// two long walls across the route, a 3 m gap between them.
	bool BuildWalls(UWorld& World, double Now)
	{
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (!FormationHeadingNeck::ChooseNeckRoute(World, *Wallet, *Force, *State, Route))
			return Fail(TEXT("A route with a path exists on the map"));
		OpenWidth = Route.NeckWidth;
		const int32 Middle = FMath::Clamp(Route.Points.Num() / 2, 6, Route.Points.Num() - 4);
		FormationHeadingNeck::BuildNeck(World, Route, Middle, NeckGap, Walls);
		Stage = 2;
		StageStarted = Now;
		return false;
	}

	bool Order(UWorld& World, double Now)
	{
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (!FormationHeadingNeck::Replan(World, *Wallet, *Force, *State, Route))
			return Fail(TEXT("The force has a path through the neck"));
		if (Route.NeckWidth > 2. * NeckGap)
			return Fail(FString::Printf(TEXT("Fixture: the walls leave a narrow corridor on the route (%.0f cm)"), Route.NeckWidth));
		Initial = FormationHeadingNeck::MeasureSpread(*Force, Route);
		Test->AddInfo(FString::Printf(TEXT("Neck route from region %d to region %d: %d samples, open ground %.0f cm wide, neck %.0f cm wide at sample %d; at the start the members span %.0f cm across and %.0f cm along"),
			Route.Start, Route.Target, Route.Points.Num(), OpenWidth, Route.NeckWidth, Route.NeckIndex, Initial.Lateral, Initial.Along));
		Stage = 3;
		StageStarted = Now;
		return false;
	}

	bool March(double Now)
	{
		if (!Force.IsValid() || Force->GetUnits().Num() != 6)
			return Fail(TEXT("The force survives its march"));
		const FormationHeadingNeck::FSpread Spread = FormationHeadingNeck::MeasureSpread(*Force, Route);
		if (Spread.Index != INDEX_NONE)
		{
			FVector4 Sum = Profile.FindOrAdd(Spread.Index);
			Profile[Spread.Index] = Sum + FVector4(Spread.Lateral, Spread.Along, 1., 0.);
		}
		if (Spread.Index != INDEX_NONE && FMath::Abs(Spread.Index - Route.NeckIndex) <= 3)
		{
			Lateral.Add(Spread.Lateral);
			Along.Add(Spread.Along);
		}
		const bool bPast = Spread.Index != INDEX_NONE && Spread.Index > Route.NeckIndex + 5;
		if (!bPast && Now - StageStarted < 100.)
			return false;
		return Verdict();
	}

	bool Verdict()
	{
		if (Lateral.Num() < 3)
			return Fail(FString::Printf(TEXT("The force's centre passed the neck while sampled (%d samples)"), Lateral.Num()));
		double MeanLateral = 0., MeanAlong = 0., MaxLateral = 0.;
		for (int32 Index = 0; Index < Lateral.Num(); ++Index)
		{
			MeanLateral += Lateral[Index] / Lateral.Num();
			MeanAlong += Along[Index] / Along.Num();
			MaxLateral = FMath::Max(MaxLateral, Lateral[Index]);
		}
		Test->AddInfo(FString::Printf(TEXT("At the neck (%d samples): mean lateral %.0f cm (max %.0f), mean along %.0f cm; at the start %.0f across, %.0f along"),
			Lateral.Num(), MeanLateral, MaxLateral, MeanAlong, Initial.Lateral, Initial.Along));
		FString Table;
		TArray<int32> Indices;
		Profile.GetKeys(Indices);
		Indices.Sort();
		for (const int32 Index : Indices)
			Table += FString::Printf(TEXT(" %d:%.0f/%.0f"), Index, Profile[Index].X / Profile[Index].Z, Profile[Index].Y / Profile[Index].Z);
		Test->AddInfo(TEXT("Mean lateral/along cm by route sample:") + Table);
		// The approach: the four samples before the neck's own window. On main the same measurement gives about 200
		// cm there and 176 cm at the neck (run 20261006-114843-test-2e20); a column on the move gives about 130 and 115.
		double Approach = 0.;
		int32 Counted = 0;
		for (int32 Index = Route.NeckIndex - 5; Index <= Route.NeckIndex - 2; ++Index)
			if (const FVector4* Sum = Profile.Find(Index))
			{
				Approach += Sum->X / Sum->Z;
				++Counted;
			}
		if (Counted < 3)
			return Fail(TEXT("The approach to the neck was sampled"));
		Approach /= Counted;
		Test->AddInfo(FString::Printf(TEXT("Approach to the neck: mean lateral %.0f cm"), Approach));
		if (Approach > ApproachBound)
			return Fail(FString::Printf(TEXT("The marching members string out into a column before the neck: lateral %.0f cm <= %.0f"), Approach, ApproachBound));
		if (MeanLateral > NeckBound)
			return Fail(FString::Printf(TEXT("The column passes the neck narrower than a box would: lateral %.0f cm <= %.0f"), MeanLateral, NeckBound));
		return true;
	}

	FAutomationTestBase* Test;
	double Started, StageStarted = 0.;
	int32 Stage = 0, Home = INDEX_NONE;
	ACommandGameState* State = nullptr;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Force;
	FormationHeadingNeck::FRoute Route;
	FormationHeadingNeck::FSpread Initial;
	TArray<double> Lateral, Along;
	TArray<TWeakObjectPtr<AActor>> Walls;
	double OpenWidth = 0.;
	TMap<int32, FVector4> Profile;
	static constexpr double NeckGap = 300.;
	// Bounds between main's measurement and the column's: see Verdict.
	static constexpr double ApproachBound = 170., NeckBound = 150.;
};
}

bool FFormationHeadingNeckTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FormationHeadingNeckTests::FScenario(this));
	return true;
}

#endif
