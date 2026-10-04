#pragma once
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "CommandCamera.h"
#include "CommandHUD.h"
#include "FailoverNode.h"
#include "HUD/OrderInputPreview.h"
#include "Engine/GameViewportClient.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HAL/PlatformTime.h"
#include "InputKeyEventArgs.h"
#include "InputCoreTypes.h"
#include "UnrealClient.h"

namespace VerbInputTests
{
using namespace ArmyTestSetup;

// Fresh standalone world: stationary explicit forces and completed producers,
// no JEV, paid production or income. Executor travel is covered by VerbOrderTests.
// Stages are split by behaviour: Setup (VerbInputSetupTests.cpp), smart orders,
// key targeting, queueing, Attack targeting, rally and camera movement.
class FScenario : public IAutomationLatentCommand
{
public:
	FScenario(FAutomationTestBase* InTest, FIntPoint InTargetSurface) : Test(InTest), TargetSurface(InTargetSurface) {}

	bool Update() override;

private:
	// VerbInputTests.cpp
	bool RunStage(UWorld* World);
	// VerbInputSetupTests.cpp
	bool Initialize(UWorld* World);
	bool Setup(UWorld* World);
	bool IsolateWorld(UWorld* World);
	bool FindRegions();
	bool SpawnFixtures(UWorld* World);
	bool FindHostileGround();
	bool SpawnForces(UWorld* World);
	ACommandBuilding* Building(UWorld* World, ACommandPlayerState* Owner, const FVector& Location);
	// VerbInputSmartOrderTests.cpp
	bool StageSmartMinimap();
	bool StageSmartGround();
	// A click on a hostile Failover Node on the ground resolves to an Attack on it, like any structure.
	bool StageSmartNode();
	bool Submit(const FVector2D& Point, ForceOrderInput::EResolution Resolution, EForceVerb Verb,
		int32 Region, AActor* Structure = nullptr, bool bQueue = false);
	bool OrdersMatch(EForceVerb Verb, int32 Region, AActor* Structure, bool bQueue);
	// VerbInputKeyTests.cpp
	bool StageAttackKey();
	bool StageRetreatKey();
	bool StageReopenAfterRetreat();
	bool StageEscape();
	bool StageReopenForRightClick();
	bool StageRightClick();
	bool ExerciseAttackPanels();
	bool ClickPanel(EHUDAction Action);
	// VerbInputQueueTests.cpp
	bool StageQueueAppend();
	bool StageQueueFullTargeting();
	bool Rejection(const FOrderInputPreview& Preview);
	bool RejectedUnchanged(const FOrderInputPreview& Preview);
	// VerbInputAttackTests.cpp
	bool StageUnshiftedConfirm();
	bool StageGroundAttackOpen();
	bool StageGroundAttackClick();
	bool AttackPreview(const FVector2D& Point, bool bQueue);
	bool ConfirmMinimapAttack();
	// VerbInputRallyTests.cpp
	bool StageRally();
	bool ExerciseMinimapRally();
	bool ExerciseGroundRallyAndBox();
	// VerbInputCameraTests.cpp
	bool StageArrowKey();
	bool StageEdgePan(UWorld* World);
	bool BeginEdgePan();
	FViewport* FocusedViewport() const;

	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
		{
			Test->AddError(Message);
			bFailed = true;
		}
		if (!Value)
			RestoreInput();
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Check(false, Message);
		return true;
	}
	void Key(const FKey& Value, EInputEvent Event)
	{
		if (IsValid(PC))
		{
			FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
			PC->InputKey(FInputKeyEventArgs(Viewport, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),
				Value, Event, FPlatformTime::Cycles64()));
		}
	}
	void RestoreInput()
	{
		for (const FKey& Value : { EKeys::A, EKeys::R, EKeys::Escape, EKeys::RightMouseButton,
				 EKeys::LeftMouseButton, EKeys::LeftShift, EKeys::Up })
			Key(Value, IE_Released);
		if (IsValid(PC) && bRestoreCursor)
			PC->SetMouseLocation(FMath::RoundToInt(OriginalMouseX), FMath::RoundToInt(OriginalMouseY));
	}
	bool Finish()
	{
		RestoreInput();
		Test->AddInfo(TEXT("Verb input: ground/minimap smart orders, multi-force region and hostile structure targets, real minimap HUD and native ground LMB A routing, Pause/Menu panel interactions, targeting/acceptance/cancellation deck state, A/R/Esc/right-click keys, Shift bounded queue, preview/order agreement, rejection retention and explanation, minimap and ground producer rally, selection-only drag and native arrow camera movement."));
		return true;
	}
	void CenterCursor()
	{
		int32 Width = 0, Height = 0;
		PC->GetViewportSize(Width, Height);
		PC->SetMouseLocation(Width / 2, Height / 2);
	}
	void SelectBoth()
	{
		PC->SelectForce(Forces[0]);
		PC->SelectForce(Forces[1], true);
	}
	FVector2D Minimap(const FVector& Point) const
	{
		FVector2D Origin;
		float Size = 0.f;
		HUD->GetMinimapScreenRect(Origin, Size);
		const FVector2D Extent = State->Arena->HalfExtent;
		return Origin + FVector2D((Point.Y + Extent.Y) / (2. * Extent.Y), (Extent.X - Point.X) / (2. * Extent.X)) * Size;
	}
	void SaveSerials()
	{
		for (int32 Index = 0; Index < 2; ++Index)
			Serials[Index] = Forces[Index]->OrderSerial;
	}
	bool Unchanged() const
	{
		return Forces[0]->OrderSerial == Serials[0] && Forces[1]->OrderSerial == Serials[1];
	}

	FAutomationTestBase* Test;
	double Started = -1.;
	bool bFailed = false;
	FIntPoint TargetSurface;
	bool bRequestedViewport = false;
	int32 Stage = 0;
	int32 Target = INDEX_NONE, EnemyHome = INDEX_NONE;
	FVector HostileRegionPoint = FVector::ZeroVector;
	FVector CameraBefore = FVector::ZeroVector;
	double EdgePanStarted = 0.;
	float OriginalMouseX = 0.f, OriginalMouseY = 0.f;
	bool bRestoreCursor = false;
	bool bEdgeOverPanel = false;
	ACommandPlayerController* PC = nullptr;
	ACommandGameState* State = nullptr;
	ACommandHUD* HUD = nullptr;
	ACommandCamera* Camera = nullptr;
	ACommandBuilding* Producer = nullptr;
	ACommandBuilding* Hostile = nullptr;
	AFailoverNode* Node = nullptr;
	bool bNodeFocused = false;
	bool bNodeClicked = false;
	AArmyGroup* Forces[2] = {};
	uint32 Serials[2] = {};
};
}
#endif
