#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CommandHUD.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HUD/HUDPanels.h"
#include "HUD/PlanningPanel.h"
#include "InputKeyEventArgs.h"
#include "PlanningFixture.h"
#include "Rules/PlanningHudPolicy.h"

// The planning world tests that drive the real controller and HUD (CoopRTS.Planning.UI.*): keys through InputKey, clicks
// through the HUD's own hit geometry, orders through the minimap, as the player does. Everything the commander edits goes
// through the controller's PlanningCommands component; the checks read the replicated planning state.
namespace PlanningUiFixture
{
class FScenario : public PlanningFixture::FScenario
{
public:
	using PlanningFixture::FScenario::FScenario;

protected:
	// The controller reads a key on its next ticks, so a stage that sent one waits with Settled(); the release follows two
	// frames later, as a held key's would.
	void Key(FKey Value)
	{
		if (Held.IsValid())
			Send(IE_Released);
		Held = Value;
		Send(IE_Pressed);
		Frames = 0;
	}
	bool Settled()
	{
		if (++Frames == 2 && Held.IsValid())
		{
			Send(IE_Released);
			Held = FKey();
		}
		return Frames > 3;
	}
	const ACommandHUD* HUD() const { return Cast<ACommandHUD>(PC->GetHUD()); }
	// A left click on the visible target of Action, through the controller's HUD entry.
	bool Click(EHUDAction Action)
	{
		FVector2D Point;
		return Check(HUD() && HUD()->FindActionScreenPosition(Action, Point), TEXT("The action has a visible hit target"))
			&& Check(PC->HandleHUDClick(Point), TEXT("A real HUD click dispatches the action"));
	}
	// Where a world point sits on the minimap, in screen pixels.
	FVector2D Minimap(const FVector& Point) const
	{
		FVector2D Origin;
		float Size = 0.f;
		HUD()->GetMinimapScreenRect(Origin, Size);
		const FVector2D Extent = State->Arena->HalfExtent;
		return Origin + FVector2D((Point.Y + Extent.Y) / (2. * Extent.Y), (Extent.X - Point.X) / (2. * Extent.X)) * Size;
	}
	const FPlanningKit* Kit() const { return State->FindKit(Host); }
	// The kits stand, JEV's matching start stands and the HUD has a viewport; false while waiting, failing at the timeout.
	bool KitsAndHudUp()
	{
		if (JevKitStands(2) && State->Planning.Kits.Num() == 2 && HUD())
		{
			PC->GetViewportSize(ViewWidth, ViewHeight);
			if (ViewWidth > 0)
				return true;
		}
		if (StageSeconds() > 20.)
			Check(false, TEXT("The kits, JEV's matching start and the HUD stand"));
		return false;
	}
	FString Feedback() const { return PC->GetOrderFeedback(); }
	// What Enter asks when nothing is placed: both pieces, by the catalogue's names.
	FString Question() const
	{
		const CommandHUDPanels::FContext Context = CommandHUDPanels::MakeContext(PC);
		TStringBuilder<128> Text;
		PlanningHud::AppendConfirm(Text, true, true, CommandHUDPanels::PlanningPieceName(Context, false), CommandHUDPanels::PlanningPieceName(Context, true));
		return FString(Text.ToView());
	}

	int32 Frames = 0;
	int32 ViewWidth = 0, ViewHeight = 0;

private:
	void Send(EInputEvent Event)
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Held, Event, FPlatformTime::Cycles64()));
	}
	FKey Held;
};
}
#endif
