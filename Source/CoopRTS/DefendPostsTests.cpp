#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "HAL/PlatformTime.h"
#include "MapRegion.h"
#include "NavigationSystem.h"
#include "WorldOverlay.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDefendPostsTest, "CoopRTS.Map.DefendPosts",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Run alone on each authored map. Observe real navigation, ground collision and
// rendered world geometry, including removal when a region changes ownership.
class FDefendPostsScenario : public IAutomationLatentCommand
{
public:
	explicit FDefendPostsScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual ~FDefendPostsScenario() override
	{
		RestoreCapture();
	}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 45.)
		{
			Test->AddError(FString::Printf(TEXT("DefendPosts timed out at stage %d"), Stage));
			return true;
		}
		UWorld* World = ArmyTestSetup::World();
		if (!World)
			return false;
		if (!bIsolated)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->Destroy();
			bIsolated = true;
		}
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* Controller = ArmyTestSetup::Controller(World);
		ACommandPlayerState* Player = Controller ? Controller->GetPlayerState<ACommandPlayerState>() : nullptr;
		UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
		if (!ArmyTestSetup::MapReady(State) || !Player || Player->CommanderIndex < 0
			|| !Navigation || Navigation->IsNavigationBuildInProgress() || World->GetTimeSeconds() < 1.f)
			return false;
		if (Stage == 0)
		{
			// No paid force or planner may change ownership while the fixture probes it.
			for (TActorIterator<ACommandBuilding> It(World); It; ++It)
				if (It->IsProducer())
					FCommandService::ConfigureProduction(It->OwningPlayerState, *It,
						It->bForceConfigured ? It->ProductionRole : static_cast<EUnitRole>(255), false);
			Overlay = AWorldOverlay::Get(World);
			if (!Check(Overlay.IsValid(), TEXT("A local game has its client world overlay"))
				|| !ValidatePosts(*State, *Navigation))
				return true;
			for (AMapRegion* Region : State->Regions)
				if (Region->RegionRole != ERegionRole::Main && IsValid(Region->Anchor))
				{
					ProbeRegion = Region;
					Capture = Region->Anchor;
					break;
				}
			if (!Check(Capture.IsValid(), TEXT("Map supplies a capturable region for marker ownership transitions")))
				return true;
			OriginalTeam = Capture->ControllingTeam;
			bOriginalTick = Capture->IsActorTickEnabled();
			Capture->SetActorTickEnabled(false);
			Stage = 1;
			StageStarted = World->GetTimeSeconds();
			return false;
		}
		if (!Check(Overlay.IsValid() && ProbeRegion.IsValid() && Capture.IsValid(), TEXT("Marker ownership fixture survives")))
			return true;
		if (World->GetTimeSeconds() - StageStarted < .15f)
			return false; // Let normal post-update ticks replace the submitted geometry.
		if (!CheckMarkers(*State, Player->TeamIndex))
			return true;
		switch (Stage)
		{
		case 1:
			Capture->ControllingTeam = Player->TeamIndex == 5 ? 0 : 5;
			break;
		case 2:
			Capture->ControllingTeam = Player->TeamIndex;
			break;
		case 3:
			Capture->ControllingTeam = -1;
			break;
		case 4:
			RestoreCapture();
			Test->AddInfo(TEXT("DefendPosts passed: all regions expose 2-3 authored posts inside their polygons on walkable ground; local markers appear on capture and disappear for enemy or neutral ownership."));
			return true;
		}
		++Stage;
		StageStarted = World->GetTimeSeconds();
		return false;
	}

private:
	bool Check(bool bCondition, const FString& What)
	{
		if (!bCondition)
			Test->AddError(What);
		return bCondition;
	}

	bool ValidatePosts(const ACommandGameState& State, UNavigationSystemV1& Navigation)
	{
		int32 PlacedRegions = 0;
		for (TActorIterator<AMapRegion> It(State.GetWorld()); It; ++It)
		{
			++PlacedRegions;
			if (!Check(State.Regions.Contains(*It), TEXT("Every placed region is exposed by the game state")))
				return false;
		}
		if (!Check(PlacedRegions == State.Regions.Num(), TEXT("Game state has exactly the placed map regions")))
			return false;
		const FNavAgentProperties& Agent = GetDefault<AArmyUnit>()->GetNavAgentPropertiesRef();
		for (const AMapRegion* Region : State.Regions)
		{
			if (!Check(IsValid(Region), TEXT("Every exposed region is valid")))
				return false;
			const TArray<FVector>& Posts = Region->GetDefendPosts();
			const FString Label = FString::Printf(TEXT("Region %d (%s)"), Region->RegionIndex, *Region->DisplayName.ToString());
			if (!Check(Posts.Num() >= 2 && Posts.Num() <= 3, Label + TEXT(" exposes 2-3 authored defend posts")))
				return false;
			for (int32 Index = 0; Index < Posts.Num(); ++Index)
			{
				const FVector& Post = Posts[Index];
				const FString PostLabel = FString::Printf(TEXT("%s post %d at %s"), *Label, Index, *Post.ToCompactString());
				if (!Check(!Post.ContainsNaN() && Region->Contains(Post), PostLabel + TEXT(" is finite and inside its polygon")))
					return false;
				FHitResult Ground;
				if (!Check(State.GetWorld()->LineTraceSingleByChannel(Ground,
							   Post + FVector(0., 0., 100.), Post - FVector(0., 0., 100.), ECC_WorldStatic)
							&& FMath::Abs(Ground.ImpactPoint.Z - Post.Z) <= 5. && Ground.ImpactNormal.Z >= .7,
						PostLabel + TEXT(" rests on walkable ground rather than an obstacle or empty space")))
					return false;
				FNavLocation Projected;
				if (!Check(Navigation.ProjectPointToNavigation(Post, Projected, FVector(75., 75., 100.), &Agent)
							&& FVector::Dist2D(Post, Projected.Location) <= 25.
							&& FMath::Abs(Post.Z - Projected.Location.Z) <= 25.,
						PostLabel + TEXT(" is navigable at its authored position")))
					return false;
			}
		}
		return true;
	}

	bool HasGroundMarker(const FVector& Post) const
	{
		TInlineComponentArray<UInstancedStaticMeshComponent*> Meshes(Overlay.Get());
		for (const UInstancedStaticMeshComponent* Mesh : Meshes)
			for (int32 Index = 0; Index < Mesh->GetInstanceCount(); ++Index)
			{
				FTransform Transform;
				if (Mesh->GetInstanceTransform(Index, Transform, true))
				{
					const FVector Center = Transform.GetLocation();
					if (FVector::Dist2D(Center, Post) < 1. && Center.Z > Post.Z && Center.Z < Post.Z + 20.)
						return true;
				}
			}
		return false;
	}

	bool CheckMarkers(const ACommandGameState& State, int32 LocalTeam)
	{
		for (const AMapRegion* Region : State.Regions)
		{
			const bool bOwned = State.GetRegionController(Region->RegionIndex) == LocalTeam;
			for (const FVector& Post : Region->GetDefendPosts())
				if (!Check(HasGroundMarker(Post) == bOwned,
						FString::Printf(TEXT("Region %d post %s marker is %s for controller %d and local team %d"),
							Region->RegionIndex, *Post.ToCompactString(), bOwned ? TEXT("visible") : TEXT("absent"),
							State.GetRegionController(Region->RegionIndex), LocalTeam)))
					return false;
		}
		return true;
	}

	void RestoreCapture()
	{
		if (Capture.IsValid())
		{
			Capture->ControllingTeam = OriginalTeam;
			Capture->SetActorTickEnabled(bOriginalTick);
			Capture.Reset();
		}
	}

	FAutomationTestBase* Test;
	TWeakObjectPtr<AWorldOverlay> Overlay;
	TWeakObjectPtr<AMapRegion> ProbeRegion;
	TWeakObjectPtr<ACapturePoint> Capture;
	int32 OriginalTeam = -1;
	int32 Stage = 0;
	bool bOriginalTick = false;
	bool bIsolated = false;
	double Started;
	float StageStarted = 0.f;
};

bool FDefendPostsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FDefendPostsScenario(this));
	return true;
}

#endif
