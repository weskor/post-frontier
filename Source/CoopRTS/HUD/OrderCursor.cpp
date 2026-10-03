#include "OrderCursor.h"
#include "CommandPlayerController.h"
#include "InputCoreTypes.h"

namespace CommandHUDPanels
{
void DrawOrderCursor(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	const ACommandPlayerController* Controller = Context.Controller;
	if (Controller->IsPlacingBuilding() || Controller->IsBuildHotkeyPending()
		|| (Controller->GetSelectedForces().IsEmpty() && (!Context.Building || !Context.Building->IsProducer())))
		return;
	float X, Y;
	if (!Controller->GetMousePosition(X, Y))
		return;
	const bool bQueue = Controller->IsInputKeyDown(EKeys::LeftShift) || Controller->IsInputKeyDown(EKeys::RightShift);
	const FOrderInputPreview Preview = Controller->GetOrderPreview(FVector2D(X, Y), bQueue);
	TStringBuilder<192> Text;
	Text << (Controller->IsAssigningOrder() ? (bQueue ? TEXT("Shift+LMB: ") : TEXT("LMB: ")) : (bQueue ? TEXT("Shift+RMB: ") : TEXT("RMB: ")))
		 << Preview.Label();
	if (Controller->IsAssigningOrder())
		Text << TEXT(" | RMB/Esc: cancel");
	const float Width = FMath::Min(Layout.Width - 2.f * Margin, Paint.TextWidth(Text.ToView(), 10.f, true) + 2.f * Pad);
	const FRect Rect{
		FMath::Clamp(static_cast<float>(X / Layout.Scale + 18.f), Margin, Layout.Width - Margin - Width),
		FMath::Clamp(static_cast<float>(Y / Layout.Scale - 32.f), Margin, Layout.Height - Margin - 28.f),
		Width, 28.f
	};
	const FLinearColor Color = !Preview.IsAllowed() ? Palette::Warn : Preview.Resolution == ForceOrderInput::EResolution::Attack ? Palette::Enemy
																																 : Palette::Good;
	Paint.Fill(Rect, Palette::Panel);
	Paint.Outline(Rect, Color);
	Paint.TextIn(Text.ToView(), Rect, 10.f, Color, true, EAlign::Left, Pad);
}
}
