#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandCamera.h"
#include "CommandMinimap.h"
#include "GroundHeight.h"
#include "HAL/PlatformTime.h"
#include "TerrainTestData.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTerrainPlateauTest, "CoopRTS.Map.Terrain.Plateau",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Run alone on Habitable Zone v2. North Ridge (region 4) is a 300 cm plateau: height-correct ground queries, then a
// real capture, a paid building placed from a cursor-plane (z = 0) request, and a produced recruit, all up there.
namespace TerrainScenarioTests
{
using namespace ArmyTestSetup;

constexpr int32 RidgeRegion = 4;
constexpr double PlateauHeight = 300.;

class FTerrainPlateau : public IAutomationLatentCommand
{
public:
	explicit FTerrainPlateau(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 200.)
			return Fail(FString::Printf(TEXT("Terrain plateau timed out at stage %d"), Stage));
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandPlayerState* Wallet = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!MapReady(State) || !Wallet || Wallet->CommanderIndex < 0 || GameSeconds(World) < 3. || !NavigationReady(World))
			return false;
		switch (Stage)
		{
		case 0:
			return Ground(World, *State, PC);
		case 1:
			return Capture(World, *State, PC);
		case 2:
			return Build(*State, Wallet);
		default:
			return Produce(*State, Wallet);
		}
	}

private:
	bool Fail(const FString& What)
	{
		Test->AddError(What);
		return true;
	}

	bool Check(bool bCondition, const FString& What)
	{
		if (!bCondition)
			Test->AddError(What);
		return bCondition;
	}

	// Ground height, camera focus and minimap picks follow the plateau, the ramp and flat ground, and ignore cover props.
	bool Ground(UWorld* World, ACommandGameState& State, ACommandPlayerController* PC)
	{
		FString Error;
		FTerrainData Data;
		if (!LoadTerrain(Data, Error))
			return Fail(Error);
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		for (ACommandBuilding* Existing : State.Buildings)
			if (IsValid(Existing) && Existing->IsProducer())
				FCommandService::ConfigureProduction(Existing->OwningPlayerState, Existing,
					Existing->bForceConfigured ? Existing->ProductionRole : static_cast<EUnitRole>(255), false);
		State.bVerificationIncomePaused = true;
		const FVector Anchor = State.GetRegionAnchor(RidgeRegion);
		const FRamp& Ramp = Data.Ramps[0];
		if (!Check(FMath::IsNearlyEqual(GroundHeight::At(*World, Anchor.X, Anchor.Y), PlateauHeight, 1.),
				TEXT("Ground under the North Ridge anchor is the 300 cm plateau"))
			|| !Check(FMath::IsNearlyEqual(GroundHeight::At(*World, Ramp.Centre.X, Ramp.Centre.Y), PlateauHeight * .5, 15.),
				TEXT("Ground at a ramp's centre is half way up"))
			|| !Check(FMath::IsNearlyEqual(GroundHeight::At(*World, Data.FirstProp.X, Data.FirstProp.Y), 0., 1.),
				TEXT("A probe over a cover prop finds the floor under it, not the prop's roof"))
			|| !Check(GroundHeight::At(*World, State.FriendlyHeadquarters->GetActorLocation().X, State.FriendlyHeadquarters->GetActorLocation().Y) < 1.,
				TEXT("Flat ground stays at z = 0")))
			return true;
		ACommandCamera* Camera = Cast<ACommandCamera>(PC->GetPawn());
		if (!Check(Camera != nullptr, TEXT("The local commander flies the command camera")))
			return true;
		Camera->FocusOn(Anchor);
		if (!Check(FMath::IsNearlyEqual(Camera->GetActorLocation().Z, PlateauHeight, 1.), TEXT("Camera focus rises onto the plateau")))
			return true;
		Camera->FocusOn(State.FriendlyHeadquarters->GetActorLocation());
		if (!Check(FMath::Abs(Camera->GetActorLocation().Z) < 1., TEXT("Camera focus returns to the floor")))
			return true;
		const FVector2D Extent = State.Arena->HalfExtent;
		const FVector2D Screen((Anchor.Y + Extent.Y) / (2. * Extent.Y) * 400., (Extent.X - Anchor.X) / (2. * Extent.X) * 400.);
		FVector Picked;
		if (!Check(CommandMinimap::ScreenToWorld(State.Arena, Screen, FVector2D::ZeroVector, 400.f, Picked)
					&& FVector::Dist2D(Picked, Anchor) < 5. && FMath::IsNearlyEqual(Picked.Z, PlateauHeight, 1.),
				TEXT("A minimap pick on the plateau resolves to plateau height")))
			return true;
		Stage = 1;
		return false;
	}

	// Real capture: a squad stands on the ridge anchor until the region flips to the local team.
	bool Capture(UWorld* World, ACommandGameState& State, ACommandPlayerController* PC)
	{
		if (!Squad.IsValid())
		{
			if (!Check(State.GetRegionController(RidgeRegion) != 0, TEXT("North Ridge starts uncontrolled")))
				return true;
			const FVector Anchor = State.GetRegionAnchor(RidgeRegion);
			Squad = SpawnGroup(World, PC, 0, FVector(Anchor.X + 150., Anchor.Y, Anchor.Z + 100.));
			return !Check(Squad.IsValid(), TEXT("Capture squad must spawn on the plateau"));
		}
		for (const AArmyUnit* Unit : Squad->GetUnits())
			if (Unit->GetActorLocation().Z < PlateauHeight - 40.)
				return Fail(TEXT("Capture squad fell off the plateau"));
		if (State.GetRegionController(RidgeRegion) != 0)
			return false;
		Stage = 2;
		return false;
	}

	// A cursor-plane request (z = 0) over the plateau builds on the plateau and charges the wallet once.
	bool Build(ACommandGameState& State, ACommandPlayerState* Wallet)
	{
		Wallet->Resources = 4000;
		const FVector Anchor = State.GetRegionAnchor(RidgeRegion);
		FVector Request = FVector::ZeroVector;
		for (int32 Ring = 0; Ring < 12 && Request.IsZero(); ++Ring)
			for (int32 Direction = 0; Direction < 32 && Request.IsZero(); ++Direction)
			{
				const double Angle = Direction * PI / 16.;
				const FVector Point = Anchor + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.) * (600. + Ring * 200.);
				FString Reason;
				if (State.FindRegionAt(Point) && State.FindRegionAt(Point)->RegionIndex == RidgeRegion
					&& State.ValidateBuildingPlacement(BarracksIndex, 0, FVector(Point.X, Point.Y, 0.), Reason))
					Request = FVector(Point.X, Point.Y, 0.);
			}
		if (!Check(!Request.IsZero(), TEXT("A captured plateau offers a valid Barracks footprint for a z = 0 cursor request")))
			return true;
		const int32 Before = Wallet->Resources;
		Building = FCommandService::PlaceBuilding(Wallet, BarracksIndex, Request).Building;
		if (!Check(Building.IsValid() && Wallet->Resources < Before, TEXT("Placement on the plateau creates one paid Barracks")))
			return true;
		const double Ground = GroundHeight::At(*State.GetWorld(), Building->GetActorLocation().X, Building->GetActorLocation().Y);
		if (!Check(FMath::IsNearlyEqual(Ground, PlateauHeight, 1.) && Building->GetActorLocation().Z >= PlateauHeight + 63. && Building->GetActorLocation().Z <= PlateauHeight + 90.,
				FString::Printf(TEXT("The Barracks stands on the plateau, 65 cm above its navmesh floor (z %.1f)"), Building->GetActorLocation().Z)))
			return true;
		Building->Tick(60.f);
		if (!Check(Building->IsComplete(), TEXT("The plateau Barracks completes")))
			return true;
		FCommandService::ConfigureProduction(Wallet, Building.Get(), EUnitRole::Frontline, true);
		Building->TickProduction(Building->GetProductionDuration());
		Stage = 3;
		return false;
	}

	bool Produce(const ACommandGameState& State, ACommandPlayerState* Wallet)
	{
		const AArmyGroup* Force = Building.IsValid() ? Building->ForceGroup.Get() : nullptr;
		if (!Check(Force != nullptr && !Force->GetUnits().IsEmpty(), TEXT("The plateau Barracks produced a recruit")))
			return true;
		for (const AArmyUnit* Unit : Force->GetUnits())
		{
			const FVector Location = Unit->GetActorLocation();
			if (!Check(Unit->IsAlive() && Location.Z > PlateauHeight - 40. && ArmyTestSetup::RegionAt(&State, Location) == RidgeRegion,
					FString::Printf(TEXT("The recruit exits onto the plateau (z %.0f, region %d)"), Location.Z, ArmyTestSetup::RegionAt(&State, Location))))
				return true;
		}
		Test->AddInfo(TEXT("Terrain plateau: ground, camera and minimap heights, capture, paid z = 0 placement and production on North Ridge."));
		return true;
	}

	FAutomationTestBase* Test;
	TWeakObjectPtr<AArmyGroup> Squad;
	TWeakObjectPtr<ACommandBuilding> Building;
	int32 Stage = 0;
	double Started;
};
}

bool FTerrainPlateauTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(TerrainScenarioTests::FTerrainPlateau(this));
	return true;
}

#endif
