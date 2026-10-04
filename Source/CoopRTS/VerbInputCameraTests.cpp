#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "VerbInputFixture.h"

namespace VerbInputTests
{
bool FScenario::StageArrowKey()
{
	Key(EKeys::Up, IE_Released);
	if (!Check(Camera->GetActorLocation().X > CameraBefore.X + 1.,
			TEXT("Native Up arrow moves the camera forward through its input binding")))
		return true;
	if (!BeginEdgePan())
		return Finish();
	++Stage;
	return false;
}

bool FScenario::StageEdgePan(UWorld* World)
{
	if (!FocusedViewport())
	{
		Test->AddInfo(TEXT("Edge-pan movement proof unavailable: native viewport focus was lost."));
		return Finish();
	}
	if (ArmyTestSetup::GameSeconds(World) - EdgePanStarted < .05)
		return false;
	if (!Check(Camera->GetActorLocation().X < CameraBefore.X - 1.,
			bEdgeOverPanel ? TEXT("Focused viewport bottom edge pans the camera over a HUD panel")
						   : TEXT("Focused viewport bottom edge pans the camera")))
		return true;
	Test->AddInfo(bEdgeOverPanel ? TEXT("Edge-pan movement observed with native viewport focus over a HUD panel.")
								 : TEXT("Edge-pan movement observed with native viewport focus; HUD panels do not intersect this viewport's edge band."));
	return Finish();
}

FViewport* FScenario::FocusedViewport() const
{
	UGameViewportClient* Client = PC->GetWorld()->GetGameViewport();
	FViewport* Viewport = Client ? Client->Viewport : nullptr;
	return Viewport && Viewport->HasFocus() && Viewport->IsForegroundWindow() ? Viewport : nullptr;
}

bool FScenario::BeginEdgePan()
{
	if (!FocusedViewport())
	{
		Test->AddInfo(TEXT("Edge-pan movement proof unavailable: viewport lacks native focus/foreground (offscreen runs do not establish native focus)."));
		return false;
	}
	int32 Width = 0, Height = 0;
	PC->GetViewportSize(Width, Height);
	PC->SetMouseLocation(Width / 2, Height - 8);
	float X, Y;
	if (!Check(PC->GetMousePosition(X, Y) && X >= 0.f && X < Width && Y >= Height - 8 && Y < Height,
			TEXT("Focused native viewport cursor reaches the inclusive bottom edge band")))
		return false;
	bEdgeOverPanel = HUD->IsPanelPoint(FVector2D(X, Y));
	Camera->FocusOn(FVector::ZeroVector);
	CameraBefore = Camera->GetActorLocation();
	EdgePanStarted = ArmyTestSetup::GameSeconds(PC->GetWorld());
	return true;
}
}
#endif
