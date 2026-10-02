#include "HUDPanels.h"
#include "CommandPlayerController.h"

namespace CommandHUDPanels
{
void DrawFeedback(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	const FRect& Strip = Layout.Feedback;
	const float Alpha = Context.Controller->GetFeedbackOpacity();
	Paint.Fill(Strip, FLinearColor(.015f, .022f, .03f, .86f * Alpha));
	FLinearColor Accent = Palette::Warn;
	Accent.A *= Alpha;
	Paint.Fill({ Strip.X, Strip.Y, 3.f, Strip.H }, Accent);
	Paint.TextIn(Context.Controller->GetOrderFeedback(), Strip, 10.f, FLinearColor(.98f, .88f, .66f, Alpha), false, EAlign::Left, 12.f);
}

}
