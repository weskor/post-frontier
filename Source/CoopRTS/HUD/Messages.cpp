#include "HUDPanels.h"
#include "CommandPlayerController.h"

namespace CommandHUDPanels
{
void DrawFeedback(const FPainter& Paint, const FContext& Context, const FLayout& Layout)
{
	const FRect& Strip = Layout.Feedback;
	Paint.Fill(Strip, FLinearColor(.015f, .022f, .03f, .86f));
	Paint.Fill({ Strip.X, Strip.Y, 3.f, Strip.H }, Palette::Warn);
	Paint.TextIn(Context.Controller->GetOrderFeedback(), Strip, 10.f, FLinearColor(.98f, .88f, .66f), false, EAlign::Left, 12.f);
}

}
