#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "AIController.h"
#include "ArmyUnit.h"
#include "Navigation/PathFollowingComponent.h"
#include "GroundHeight.h"
#include "TerrainTestData.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainRoutesTest, "CoopRTS.Map.Terrain.Routes",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Run alone on Habitable Zone v2: real squads walk every authored route and ramp on the real navmesh.
namespace TerrainScenarioTests
{
using namespace ArmyTestSetup;

// One squad, one list of single-region hops. A hop is a neighbour order, so the squad cannot skip a ramp.
struct FLeg
{
	FString Label;
	FVector Start = FVector::ZeroVector;
	TArray<int32> Hops;
	TArray<const FRamp*> MustCross;
};

class FTerrainRoutes : public IAutomationLatentCommand
{
public:
	explicit FTerrainRoutes(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 420. || (Stage > 0 && FPlatformTime::Seconds() - LegStarted > 60.))
		{
			Describe();
			return Fail(FString::Printf(TEXT("Terrain routes timed out in leg %d hop %d"), LegIndex, Hop));
		}
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		Controller = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandPlayerState* Wallet = Controller.IsValid() ? Controller->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!MapReady(State) || !Wallet || Wallet->CommanderIndex < 0 || GameSeconds(World) < 3. || !NavigationReady(World))
			return false;
		if (Stage == 0)
			return Setup(World, *State);
		if (!Force.IsValid())
			return BeginLeg(World, *State, Wallet);
		Track();
		return Advance(*State, Wallet);
	}

private:
	bool Fail(const FString& What)
	{
		Test->AddError(What);
		return true;
	}

	void Describe() const
	{
		if (!Force.IsValid())
			return;
		Test->AddInfo(FString::Printf(TEXT("game=%.1fs force status=%d holding=%d hold_region=%d target=%d post=%d responding=%d current_region=%d"),
			ArmyTestSetup::GameSeconds(Force->GetWorld()), static_cast<int32>(Force->Status), Force->IsHoldingRegion(), Force->HoldRegionIndex,
			Force->TargetRegionIndex, Force->HoldPostIndex, Force->bHoldResponding, CurrentRegion(Force.Get())));
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			const AAIController* AI = Cast<AAIController>(Unit->GetController());
			const UPathFollowingComponent* Path = AI ? AI->GetPathFollowingComponent() : nullptr;
			Test->AddInfo(FString::Printf(TEXT("unit position=%s speed=%.0f post_distance=%.0f path_status=%d slot=%d reinforcing=%d alive=%d goal=%s"), *Unit->GetActorLocation().ToString(),
				Unit->GetVelocity().Size2D(), FVector::Dist2D(Unit->GetActorLocation(), Force->HoldPostLocation),
				Path ? static_cast<int32>(Path->GetStatus()) : -1, Unit->GetCompositionSlot(), Unit->IsReinforcing(), Unit->IsAlive(),
				Path ? *Path->GetPathDestination().ToString() : TEXT("none")));
		}
	}

	bool Setup(UWorld* World, ACommandGameState& State)
	{
		FString Error;
		if (!LoadTerrain(Data, Error))
			return Fail(Error);
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		for (ACommandBuilding* Building : State.Buildings)
			if (IsValid(Building) && Building->IsProducer())
				FCommandService::ConfigureProduction(Building->OwningPlayerState, Building,
					Building->bForceConfigured ? Building->ProductionRole : static_cast<EUnitRole>(255), false);
		State.bVerificationIncomePaused = true;
		const FVector Home = FromFriendlyHQ(&State, 800.f, 800.f, 0.f);
		for (const FRoute& Route : Data.Routes)
		{
			FLeg& Leg = Legs.AddDefaulted_GetRef();
			Leg.Label = Route.Name;
			Leg.Start = Home;
			for (int32 Index = 1; Index + 1 < Route.Regions.Num(); ++Index)
				Leg.Hops.Add(Route.Regions[Index]);
			for (const FRamp& Ramp : Data.Ramps)
			{
				const int32 At = Route.Regions.Find(Ramp.Plateau), Other = Route.Regions.Find(Ramp.To);
				if (At != INDEX_NONE && Other != INDEX_NONE && FMath::Abs(At - Other) == 1)
					Leg.MustCross.Add(&Ramp);
			}
		}
		for (const FRamp& Ramp : Data.Ramps)
		{
			FLeg& Leg = Legs.AddDefaulted_GetRef();
			Leg.Label = FString::Printf(TEXT("Ramp %d->%d"), Ramp.Plateau, Ramp.To);
			const FVector2D Foot = Ramp.Centre + Downhill(Ramp) * 1400.;
			Leg.Start = FVector(Foot.X, Foot.Y, 0.);
			Leg.Hops.Add(Ramp.Plateau);
			Leg.MustCross.Add(&Ramp);
		}
		Stage = 1;
		LegStarted = FPlatformTime::Seconds();
		return false;
	}

	bool BeginLeg(UWorld* World, ACommandGameState& State, ACommandPlayerState* Wallet)
	{
		if (LegIndex == Legs.Num())
		{
			Test->AddInfo(FString::Printf(TEXT("Terrain routes: %d squads walked %d authored routes and %d ramps to hold arrivals on the real navmesh."),
				Legs.Num(), Data.Routes.Num(), Data.Ramps.Num()));
			return true;
		}
		const FLeg& Leg = Legs[LegIndex];
		const FVector Start(Leg.Start.X, Leg.Start.Y, GroundHeight::At(*World, Leg.Start.X, Leg.Start.Y) + 100.);
		Force = SpawnGroup(World, Controller.Get(), 0, Start);
		if (!Force.IsValid())
			return Fail(Leg.Label + TEXT(": squad fixture must spawn"));
		Hop = 0;
		Closest.Init(TNumericLimits<double>::Max(), Leg.MustCross.Num());
		OnDeck.Init(false, Leg.MustCross.Num());
		LegStarted = FPlatformTime::Seconds();
		return IssueHop(State, Wallet) ? false : true;
	}

	bool IssueHop(ACommandGameState& State, ACommandPlayerState* Wallet)
	{
		const FLeg& Leg = Legs[LegIndex];
		const int32 Target = Leg.Hops[Hop];
		const AMapRegion* From = State.FindRegionAt(Force->GetCenter());
		if (!From || !From->Neighbours.Contains(Target) || !FCommandService::IssueForceOrder(Wallet, Force.Get(), EForceVerb::MoveHold, Target))
		{
			Fail(FString::Printf(TEXT("%s: order to region %d from region %d rejected"), *Leg.Label, Target, From ? From->RegionIndex : -1));
			return false;
		}
		return true;
	}

	void Track()
	{
		const FLeg& Leg = Legs[LegIndex];
		for (int32 Index = 0; Index < Leg.MustCross.Num(); ++Index)
			for (const AArmyUnit* Unit : Force->GetUnits())
			{
				const FVector Location = Unit->GetActorLocation();
				const double Distance = FVector2D::Distance(FVector2D(Location), Leg.MustCross[Index]->Centre);
				Closest[Index] = FMath::Min(Closest[Index], Distance);
				// Halfway up the deck: a unit inside the footprint at a height between the floor and the plateau.
				OnDeck[Index] |= Distance < 450. && Location.Z > 100. && Location.Z < 300.;
			}
	}

	// Holding the region with every member stopped at the post, on the level the region stands on.
	bool Arrived(int32 Target, double& OutLowestZ) const
	{
		if (!Force->IsHoldingRegion() || Force->HoldRegionIndex != Target || Force->TargetRegionIndex != Target
			|| Force->bHoldResponding || Force->HoldPostIndex == INDEX_NONE || CurrentRegion(Force.Get()) != Target)
			return false;
		OutLowestZ = TNumericLimits<double>::Max();
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			if (Unit->GetVelocity().Size2D() > 5.f || FVector::Dist2D(Unit->GetActorLocation(), Force->HoldPostLocation) > 450.f)
				return false;
			OutLowestZ = FMath::Min(OutLowestZ, static_cast<double>(Unit->GetActorLocation().Z));
		}
		return !Force->GetUnits().IsEmpty();
	}

	bool Advance(ACommandGameState& State, ACommandPlayerState* Wallet)
	{
		const FLeg& Leg = Legs[LegIndex];
		double LowestZ = 0.;
		if (!Arrived(Leg.Hops[Hop], LowestZ))
			return false;
		const AMapRegion* Region = State.FindRegionAt(State.GetRegionAnchor(Leg.Hops[Hop]));
		const bool bPlateau = Region && GroundHeight::At(*Force->GetWorld(), State.GetRegionAnchor(Region->RegionIndex).X, State.GetRegionAnchor(Region->RegionIndex).Y) > 250.;
		if (bPlateau && LowestZ < 250.)
			return Fail(FString::Printf(TEXT("%s: hold in plateau region %d must stand on the plateau (lowest unit z %.0f)"), *Leg.Label, Leg.Hops[Hop], LowestZ));
		if (++Hop < Leg.Hops.Num())
			return IssueHop(State, Wallet) ? false : true;
		for (int32 Index = 0; Index < Leg.MustCross.Num(); ++Index)
			if (Closest[Index] > 500. || !OnDeck[Index])
				return Fail(FString::Printf(TEXT("%s: squad never climbed ramp %d->%d (closest %.0f cm, on deck %d)"), *Leg.Label,
					Leg.MustCross[Index]->Plateau, Leg.MustCross[Index]->To, Closest[Index], OnDeck[Index]));
		for (AArmyUnit* Unit : TArray<AArmyUnit*>(Force->GetUnits()))
			if (IsValid(Unit))
				Unit->Destroy();
		Force->Destroy();
		Force.Reset();
		++LegIndex;
		return false;
	}

	FAutomationTestBase* Test;
	FTerrainData Data;
	TArray<FLeg> Legs;
	TArray<double> Closest;
	TArray<bool> OnDeck;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<AArmyGroup> Force;
	int32 Stage = 0;
	int32 LegIndex = 0;
	int32 Hop = 0;
	double Started;
	double LegStarted = 0.;
};
}

bool FTerrainRoutesTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(TerrainScenarioTests::FTerrainRoutes(this));
	return true;
}

#endif
