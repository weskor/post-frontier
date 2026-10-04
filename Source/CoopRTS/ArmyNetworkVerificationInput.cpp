// Probe actions that drive the shared controller and HUD paths (selection, HUD clicks, keys, cursor, capture).
// These are not native OS input; commands still use the owning-controller RPCs.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandHUD.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Engine/Engine.h"
#include "Engine/GameViewportClient.h"
#include "EngineUtils.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "InputKeyEventArgs.h"
#include "Json.h"
#include "UnrealClient.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
FString InputSelectBuilding(const FProbeRequest& Probe)
{
	ACommandPlayerController* PC = Probe.PC;
	const ACommandGameState* State = Probe.State;
	const ACommandPlayerState* Own = Probe.Own;
	const int32 BuildingIndex = Probe.Request->GetIntegerField(TEXT("building"));
	if (!State || !State->Buildings.IsValidIndex(BuildingIndex) || !IsValid(State->Buildings[BuildingIndex]))
		return TEXT("building not replicated locally");
	bool bDoubleClick = false;
	Probe.Request->TryGetBoolField(TEXT("doubleClick"), bDoubleClick);
	ACommandBuilding* Building = State->Buildings[BuildingIndex];
	PC->SelectActorWithModifiers(Building, false, bDoubleClick);
	if (Building->OwningPlayerState != Own)
	{
		AArmyGroup* Force = Building->ForceGroup;
		if (!Own || !IsValid(Building->OwningPlayerState)
			|| Building->OwningPlayerState->TeamIndex != Own->TeamIndex || !Building->IsProducer()
			|| !IsValid(Force) || PC->GetInspectedForce() != Force
			|| PC->GetSelectedBuilding() || PC->IsForceSelected(Force))
			return TEXT("teammate force inspection rejected");
	}
	else if (!bDoubleClick && PC->GetSelectedBuilding() != Building)
		return TEXT("building selection rejected");
	return FString();
}

FString InputClickForceBadge(const FProbeRequest& Probe, AArmyGroup* Force)
{
	const ACommandHUD* HUD = Cast<ACommandHUD>(Probe.PC->GetHUD());
	FVector2D Position;
	if (!HUD || !HUD->FindForceScreenPosition(Force, Position) || HUD->GetForceAtScreenPosition(Position) != Force)
		return TEXT("force badge not visible or obscured");
	if (!Probe.PC->HandleHUDClick(Position))
		return TEXT("force badge click rejected");
	return FString();
}

FString InputSelectUnit(const FProbeRequest& Probe, AArmyGroup* Force, bool bToggle)
{
	AArmyUnit* Unit = nullptr;
	for (AArmyUnit* Candidate : Force->GetUnits())
		if (IsValid(Candidate) && Candidate->IsAlive())
		{
			Unit = Candidate;
			break;
		}
	if (!Unit)
		return TEXT("force has no living unit");
	Probe.PC->SelectActorWithModifiers(Unit, bToggle, false);
	return FString();
}

FString InputSelectForce(const FProbeRequest& Probe, const FString& Target)
{
	ACommandPlayerController* PC = Probe.PC;
	const int32 ForceNumber = Probe.Request->GetIntegerField(TEXT("number"));
	AArmyGroup* Force = nullptr;
	for (TActorIterator<AArmyGroup> It(Probe.World); It; ++It)
		if (IsValid(It->GetOwningPlayerState()) && It->GetOwningPlayerState()->CommanderIndex == Probe.Owner
			&& It->ForceNumber == ForceNumber)
		{
			Force = *It;
			break;
		}
	if (!Force)
		return TEXT("force not replicated locally");
	bool bToggle = false;
	Probe.Request->TryGetBoolField(TEXT("toggle"), bToggle);
	FString Error;
	if (Target == TEXT("badge"))
		Error = InputClickForceBadge(Probe, Force);
	else if (Target == TEXT("unit"))
		Error = InputSelectUnit(Probe, Force, bToggle);
	else
		PC->SelectForce(Force, bToggle);
	if (!Error.IsEmpty())
		return Error;
	if (PC->GetInspectedForce() != Force && !(bToggle && !PC->IsForceSelected(Force)))
		return TEXT("force inspection rejected");
	return FString();
}

FString InputSelect(const FProbeRequest& Probe)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	const FString Target = Request->GetStringField(TEXT("target"));
	if (Target == TEXT("none"))
		Probe.PC->SelectActor(nullptr);
	else if (Target == TEXT("box"))
	{
		bool bAdd = false;
		Request->TryGetBoolField(TEXT("add"), bAdd);
		Probe.PC->SelectForceBox(
			FVector2D(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y"))),
			FVector2D(Request->GetNumberField(TEXT("x2")), Request->GetNumberField(TEXT("y2"))), bAdd);
	}
	else if (Target == TEXT("building"))
		return InputSelectBuilding(Probe);
	else if (Target == TEXT("force") || Target == TEXT("unit") || Target == TEXT("badge"))
		return InputSelectForce(Probe, Target);
	else
		return TEXT("unknown selection target");
	return FString();
}

FString InputCursor(const FProbeRequest& Probe)
{
	FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	const double X = Probe.Request->GetNumberField(TEXT("x")), Y = Probe.Request->GetNumberField(TEXT("y"));
	if (!Viewport || !FMath::IsFinite(X) || !FMath::IsFinite(Y)
		|| X < 0 || Y < 0 || X >= Viewport->GetSizeXY().X || Y >= Viewport->GetSizeXY().Y)
		return TEXT("cursor screen point outside viewport");
	Viewport->SetMouse(FMath::RoundToInt(X), FMath::RoundToInt(Y));
	return FString();
}

FString InputHudAction(const FProbeRequest& Probe)
{
	const ACommandHUD* HUD = Cast<ACommandHUD>(Probe.PC->GetHUD());
	const EHUDAction HUDAction = static_cast<EHUDAction>(Probe.Request->GetIntegerField(TEXT("hudAction")));
	FVector2D Position;
	if (!HUD || !HUD->FindActionScreenPosition(HUDAction, Position))
		return TEXT("HUD action not visible");
	if (HUD->GetActionAtScreenPosition(Position) != HUDAction)
		return TEXT("HUD hit test disagrees with drawn geometry");
	if (!Probe.PC->HandleHUDClick(Position))
		return TEXT("HUD click missed every panel");
	return FString();
}

FString InputHudClick(const FProbeRequest& Probe)
{
	const FVector2D Position(Probe.Request->GetNumberField(TEXT("x")), Probe.Request->GetNumberField(TEXT("y")));
	if (!FMath::IsFinite(Position.X) || !FMath::IsFinite(Position.Y))
		return TEXT("invalid HUD screen point");
	return Probe.PC->HandleHUDClick(Position) ? FString() : TEXT("HUD click missed every panel");
}

bool InputKeySupported(const FString& KeyName)
{
	static const TCHAR* const Supported[] = { TEXT("Escape"), TEXT("F4"), TEXT("Tab"), TEXT("Enter"), TEXT("SpaceBar"),
		TEXT("F"), TEXT("One"), TEXT("Two"), TEXT("Three"), TEXT("Four"), TEXT("Five"), TEXT("Left"), TEXT("LeftShift"),
		TEXT("RightShift"), TEXT("Q"), TEXT("H"), TEXT("R"), TEXT("P"), TEXT("G"), TEXT("B"), TEXT("W"), TEXT("E"),
		TEXT("LeftMouseButton"), TEXT("T"), TEXT("A"), TEXT("RightMouseButton"), TEXT("MouseScrollUp"), TEXT("MouseScrollDown") };
	for (const TCHAR* Key : Supported)
		if (KeyName == Key)
			return true;
	return false;
}

FString InputKey(const FProbeRequest& Probe)
{
	// Delivered to PlayerInput (Enhanced Input mappings), not the OS/compositor.
	const FString KeyName = Probe.Request->GetStringField(TEXT("key"));
	if (!InputKeySupported(KeyName))
		return TEXT("unsupported probe key");
	FViewport* Viewport = GEngine && GEngine->GameViewport ? GEngine->GameViewport->Viewport : nullptr;
	Probe.PC->InputKey(FInputKeyEventArgs(Viewport, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(),
		FKey(*KeyName), Probe.Request->GetBoolField(TEXT("pressed")) ? IE_Pressed : IE_Released, FPlatformTime::Cycles64()));
	return FString();
}

FString InputScreenshot(const FProbeRequest& Probe)
{
	const FString Path = Probe.Request->GetStringField(TEXT("path"));
	if (Path.IsEmpty())
		return TEXT("screenshot path required");
	FScreenshotRequest::RequestScreenshot(Path, false, false);
	return FString();
}

FString InputResolution(const FProbeRequest& Probe)
{
	const int32 Width = static_cast<int32>(Probe.Request->GetIntegerField(TEXT("width")));
	const int32 Height = static_cast<int32>(Probe.Request->GetIntegerField(TEXT("height")));
	if (Width < 640 || Height < 480 || Width > 7680 || Height > 4320)
		return TEXT("resolution out of bounds");
	Probe.PC->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"), Width, Height));
	return FString();
}

FString InputDispatch(const FProbeRequest& Probe)
{
	const FString& Action = Probe.Action;
	if (!Probe.PC)
		return TEXT("local controller unavailable");
	if (Action == TEXT("select"))
		return InputSelect(Probe);
	if (Action == TEXT("cursor"))
		return InputCursor(Probe);
	if (Action == TEXT("hud"))
		return InputHudAction(Probe);
	if (Action == TEXT("hudClick"))
		return InputHudClick(Probe);
	if (Action == TEXT("key"))
		return InputKey(Probe);
	if (Action == TEXT("screenshot"))
		return InputScreenshot(Probe);
	return InputResolution(Probe);
}
}

bool HandleInputAction(const FProbeRequest& Probe, FString& Error)
{
	const FString& Action = Probe.Action;
	if (Action != TEXT("select") && Action != TEXT("hud") && Action != TEXT("hudClick") && Action != TEXT("key")
		&& Action != TEXT("screenshot") && Action != TEXT("resolution") && Action != TEXT("cursor"))
		return false;
	Error = InputDispatch(Probe);
	return true;
}
}
#endif
