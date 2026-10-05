#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Camera/CameraComponent.h"
#include "CapturePoint.h"
#include "Commands/CommandService.h"
#include "CommandBuilding.h"
#include "CommandCamera.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Content/MatchContent.h"
#include "Engine/StaticMesh.h"
#include "HAL/FileManager.h"
#include "GameFramework/SpringArmComponent.h"
#include "Misc/Paths.h"
#include "PlanningUiFixture.h"
#include "Rules/PlacementPolicy.h"
#include "UnrealClient.h"
#include "WorldOverlay.h"

// The rule: presentation and input run while the world is paused, the simulation does not. The mouse wheel zooms the
// camera, the arrow keys pan it, the Barracks placement preview draws its cells at the cursor, and a producer's mesh
// follows its configured unit type, both while planning stands the world still and while a solo player has paused the match.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPausedPresentationPlanningTest, "CoopRTS.Presentation.Paused.Planning",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPausedPresentationActiveTest, "CoopRTS.Presentation.Paused.ActivePause",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PausedPresentationTests
{
// The footprint cells sit 12 cm above the ground (DrawPlacementOverlay).
constexpr double FootprintHeight = 12.;

// The green footprint cells the overlay has drawn on the given centre.
int32 GreenFootprintCells(UWorld* World, const FVector& Center, double Half)
{
	int32 Count = 0;
	TInlineComponentArray<UInstancedStaticMeshComponent*> Meshes(AWorldOverlay::Get(World));
	for (const UInstancedStaticMeshComponent* Mesh : Meshes)
	{
		if (!Mesh->GetStaticMesh() || Mesh->GetStaticMesh()->GetName() != TEXT("Plane"))
			continue;
		for (int32 Index = 0; Index < Mesh->GetInstanceCount(); ++Index)
		{
			FTransform Transform;
			if (!Mesh->GetInstanceTransform(Index, Transform, true))
				continue;
			const FVector At = Transform.GetLocation();
			const float* Color = &Mesh->PerInstanceSMCustomData[Index * Mesh->NumCustomDataFloats];
			Count += FMath::Abs(At.X - Center.X) < Half && FMath::Abs(At.Y - Center.Y) < Half
				&& FMath::Abs(At.Z - (Center.Z + FootprintHeight)) < 1. && Color[1] > .5f && Color[0] < .2f;
		}
	}
	return Count;
}

// The line segments of the ring the overlay has drawn around a capture point: forty-eight segments, 9 cm above the ground.
int32 RingSegments(UWorld* World, const ACapturePoint& Point)
{
	int32 Count = 0;
	TInlineComponentArray<UInstancedStaticMeshComponent*> Meshes(AWorldOverlay::Get(World));
	for (const UInstancedStaticMeshComponent* Mesh : Meshes)
	{
		if (!Mesh->GetStaticMesh() || Mesh->GetStaticMesh()->GetName() != TEXT("Cube"))
			continue;
		for (int32 Index = 0; Index < Mesh->GetInstanceCount(); ++Index)
		{
			FTransform Transform;
			if (!Mesh->GetInstanceTransform(Index, Transform, true))
				continue;
			const FVector At = Transform.GetLocation();
			const double Radius = FVector::Dist2D(At, Point.GetActorLocation());
			Count += FMath::Abs(Radius - ACapturePoint::CaptureRadius) < ACapturePoint::CaptureRadius * .01
				&& FMath::Abs(At.Z - (Point.GetActorLocation().Z + 9.)) < 1.;
		}
	}
	return Count;
}

class FScenario final : public PlanningUiFixture::FScenario
{
public:
	FScenario(FAutomationTestBase* InTest, bool bInActivePause)
		: PlanningUiFixture::FScenario(InTest), bActivePause(bInActivePause)
	{
	}

private:
	// The match runs again and the fixture building goes, for whatever test follows in this world.
	void Cleanup() override
	{
		if (State && PC && State->IsActivePaused())
			FCommandService::Resume(PC);
		if (IsValid(Barracks))
			Barracks->Destroy();
	}

	enum EStage : int32
	{
		Boot,
		Pause,
		PauseUp,
		Wheel,
		Zoom,
		Pan,
		Arm,
		Preview,
		Capture,
		Disarm,
		Configure,
		Variant,
	};

	bool Step() override
	{
		switch (Stage)
		{
		case Boot:
			return KitsAndHudUp() ? Advance(bActivePause ? Pause : Wheel) : false;
		case Pause:
			return StartActivePause();
		case PauseUp:
			return Settled() && ActivePauseUp() ? Press(EKeys::MouseScrollUp, Zoom) : false;
		case Wheel:
			return Settled() && FrozenWorld() ? Press(EKeys::MouseScrollUp, Zoom) : false;
		case Zoom:
			return Settled() && Zoomed() ? Press(EKeys::Up, Pan) : false;
		case Pan:
			return Settled() && Panned() ? Advance(Arm) : false;
		case Arm:
			return Settled() && ArmPlacement() ? Advance(Preview) : false;
		case Preview:
			return Settled() && PreviewDrawn() ? Advance(Capture) : false;
		case Capture:
			return Captured() ? Press(EKeys::Escape, Disarm) : false;
		case Disarm:
			return Settled() && Disarmed() ? (bActivePause ? Advance(Configure) : Done()) : false;
		case Configure:
			return ConfigureBarracks() ? Advance(Variant) : false;
		default:
			return Settled() && VariantShown() ? Done() : false;
		}
	}

	bool Advance(int32 Next)
	{
		Enter(Next);
		Frames = 0;
		return false;
	}
	// Sends a key and remembers the view it must change.
	bool Press(FKey Value, int32 Next)
	{
		Before = CaptureView();
		Key(Value);
		return Advance(Next);
	}

	// Planning ends here; a finished Barracks stands in the world, the wallet pays for placement and P pauses the match.
	bool StartActivePause()
	{
		ACommandGameState::bPlanningHeldByTest = false;
		State->CompletePlanningForHarness(false);
		Host->Resources = 1000;
		FVector Second;
		if (!Check(!State->IsPlanning() && !World->IsPaused(), TEXT("Planning ended and the world runs before the solo pause"))
			|| !Check(BarracksSpot(0, Spot) && BarracksSpot(1, Second), TEXT("Two legal Barracks spots exist")))
			return true;
		Barracks = SpawnBarracks(Second);
		if (!Check(IsValid(Barracks) && Barracks->IsComplete(), TEXT("A finished Barracks stands before the pause")))
			return true;
		return Press(EKeys::P, PauseUp);
	}

	ACommandBuilding* SpawnBarracks(const FVector& Location)
	{
		const FTransform Transform(Location);
		ACommandBuilding* Result = World->SpawnActorDeferred<ACommandBuilding>(
			ACommandBuilding::StaticClass(), Transform, PC, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!Result)
			return nullptr;
		Result->BuildingIndex = ArmyTestSetup::BarracksIndex;
		Result->OwningPlayerState = Host;
		Result->TeamIndex = Host->TeamIndex;
		Result->ConstructionProgress = 1.f;
		Result->FinishSpawning(Transform);
		return Result;
	}

	bool ActivePauseUp()
	{
		return Check(State->IsActivePaused() && World->IsPaused() && !State->IsPlanning(), TEXT("P pauses the running match"))
			&& FrozenWorld();
	}

	// The world is paused and its clock stands still across the whole scenario.
	bool FrozenWorld()
	{
		if (!bClockKnown)
		{
			bClockKnown = true;
			FrozenAt = World->GetTimeSeconds();
		}
		return Check(World->IsPaused() && (bActivePause || State->IsPlanning()), TEXT("The world is paused for the whole scenario"))
			&& Check(World->GetTimeSeconds() == FrozenAt, TEXT("The simulation clock does not advance while paused"));
	}

	struct FView
	{
		double Arm = 0.;
		FVector Root = FVector::ZeroVector;
		FVector Eye = FVector::ZeroVector;
	};

	ACommandCamera* Camera() const { return Cast<ACommandCamera>(PC->GetPawn()); }
	// The arm length that was asked for, and where the camera component actually is.
	FView CaptureView() const
	{
		FView View;
		if (const ACommandCamera* Rig = Camera())
		{
			View.Arm = Rig->FindComponentByClass<USpringArmComponent>()->TargetArmLength;
			View.Root = Rig->GetActorLocation();
			View.Eye = Rig->FindComponentByClass<UCameraComponent>()->GetComponentLocation();
		}
		return View;
	}

	// The wheel changed the arm AND the rendered camera follows it: the eye stands one arm length from the pivot.
	bool Zoomed()
	{
		const FView Now = CaptureView();
		return FrozenWorld()
			&& Check(Camera() && Camera()->IsMotionBlurSuppressed(), TEXT("Motion blur is held off on the paused world's camera"))
			&& Check(Now.Arm < Before.Arm * .9, TEXT("The mouse wheel zooms in while paused"))
			&& Check(FMath::IsNearlyEqual(FVector::Dist(Now.Eye, Now.Root), Now.Arm, 2.),
				TEXT("The camera component follows the zoomed arm while paused"));
	}

	bool Panned()
	{
		const FView Now = CaptureView();
		return FrozenWorld()
			&& Check(Now.Root.X > Before.Root.X + 1. && Now.Eye.X > Before.Eye.X + 1.,
				TEXT("The Up arrow pans the camera, and its view with it, while paused"));
	}

	bool ArmPlacement()
	{
		if (!bActivePause && !Check(BarracksSpot(0, Spot), TEXT("A legal Barracks spot exists")))
			return false;
		if (!Check(Camera() != nullptr, TEXT("The commander has a camera")))
			return false;
		Camera()->FocusOn(Spot);
		Click(EHUDAction::BuildSlot0);
		Frames = 0;
		bCursorSet = false;
		return Check(PC->IsPlacingBuilding() && PC->GetPlacementIndex() == 0, TEXT("The Barracks card arms placement"));
	}

	// The focus has settled; the cursor goes onto the spot and the overlay must show the footprint under it.
	bool PreviewDrawn()
	{
		FVector2D Screen;
		if (!bCursorSet)
		{
			if (!Check(PC->ProjectWorldLocationToScreen(Spot, Screen), TEXT("The spot is on screen after the camera moved")))
				return false;
			PC->SetMouseLocation(FMath::RoundToInt(Screen.X), FMath::RoundToInt(Screen.Y));
			bCursorSet = true;
			Frames = 0;
			return false;
		}
		FVector Location;
		FString Reason;
		bool bCanPlace = false;
		const UBuildingDefinition* Definition = PC->GetPlacementDefinition();
		if (!Check(Definition && PC->GetPlacementPreview(Location, Reason, bCanPlace), TEXT("The cursor is over ground")))
			return false;
		const int32 Cells = PlacementPolicy::FootprintCells(ACommandBuilding::GetFootprintRadius(*Definition));
		const double Half = Cells * PlacementPolicy::BuildGridCellSize * .5;
		return FrozenWorld()
			&& Check(bCanPlace && FVector::Dist2D(Location, Spot) < 100.,
				FString::Printf(TEXT("The preview follows the cursor onto a legal spot (%s)"), *Reason))
			&& Check(GreenFootprintCells(World, Location, Half) == Cells * Cells,
				TEXT("Placement shows every green footprint cell at the cursor while paused"))
			&& RingsDrawn();
	}

	// Rings are presentation of a capture point's replicated state; its own tick is stopped while paused.
	bool RingsDrawn()
	{
		return Check(!State->CaptureSites.IsEmpty() && IsValid(State->CaptureSites[0]), TEXT("The map has a capture point"))
			&& Check(RingSegments(World, *State->CaptureSites[0]) >= 48, TEXT("A capture point's ring is drawn while paused"));
	}

	FString CapturePath() const
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("PausedPresentationCaptures")
			/ (bActivePause ? TEXT("active-pause-placement.png") : TEXT("planning-placement.png")));
	}

	// The zoomed, panned view with the armed placement's cells on screen, for a person to inspect.
	bool Captured()
	{
		if (Frames == 0)
			IFileManager::Get().Delete(*CapturePath());
		if (++Frames == 10)
			FScreenshotRequest::RequestScreenshot(CapturePath(), false, false);
		if (Frames > 10 && IFileManager::Get().FileSize(*CapturePath()) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Captured %s (%lld bytes), paused %d, planning %d"), *CapturePath(),
				IFileManager::Get().FileSize(*CapturePath()), World->IsPaused(), State->IsPlanning()));
			return true;
		}
		if (Frames > 900)
			Check(false, TEXT("No screenshot was produced"));
		return false;
	}

	bool Disarmed()
	{
		return FrozenWorld() && Check(!PC->IsPlacingBuilding(), TEXT("Esc ends the armed placement while paused"));
	}

	// The first role whose Barracks mesh variant differs from the neutral mesh.
	bool VariantFor(int32& OutUnit, UStaticMesh*& OutMesh) const
	{
		const UBuildingDefinition* Definition = Barracks->GetDefinition();
		if (!Definition)
			return false;
		UStaticMesh* Neutral = Definition->HumanMesh.LoadSynchronous();
		for (int32 Index = 0; Index < Definition->HumanRoleMeshes.Num(); ++Index)
		{
			UStaticMesh* Variant = Definition->HumanRoleMeshes[Index].LoadSynchronous();
			if (Variant && Variant != Neutral && State->Content->Unit(Index))
			{
				OutUnit = Index;
				OutMesh = Variant;
				return true;
			}
		}
		return false;
	}

	// The commander picks the Barracks' unit type while paused: the server applies it at once (no time passes).
	bool ConfigureBarracks()
	{
		int32 Unit = INDEX_NONE;
		UStaticMesh* Variant = nullptr;
		if (!Check(VariantFor(Unit, Variant), TEXT("The Barracks has a role mesh that differs from its neutral mesh")))
			return false;
		const FCommandResult Result = FCommandService::ConfigureProduction(Host, Barracks, State->Content->Unit(Unit)->Role, true);
		return Check(Result.IsAccepted() && Barracks->bForceConfigured, FString::Printf(TEXT("Configuring production is accepted while paused (%s)"), *Result.Message));
	}

	bool VariantShown()
	{
		int32 Unit = INDEX_NONE;
		UStaticMesh* Variant = nullptr;
		const UStaticMeshComponent* Body = Barracks->FindComponentByClass<UStaticMeshComponent>();
		return FrozenWorld() && Check(VariantFor(Unit, Variant) && Body && Body->GetStaticMesh() == Variant, TEXT("The producer's mesh follows its configured unit type while paused"));
	}

	bool bActivePause;
	bool bClockKnown = false;
	bool bCursorSet = false;
	double FrozenAt = 0.;
	FView Before;
	FVector Spot = FVector::ZeroVector;
	ACommandBuilding* Barracks = nullptr;
};
}

bool FPausedPresentationPlanningTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PausedPresentationTests::FScenario(this, false));
	return true;
}

bool FPausedPresentationActiveTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PausedPresentationTests::FScenario(this, true));
	return true;
}
#endif
