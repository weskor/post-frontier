#include "HUDPanels.h"
#include "CommandMinimap.h"
#include "CommandPlayerController.h"

namespace CommandHUDPanels
{
void DrawMinimap(const FPainter& Paint, ACommandPlayerController* Controller, const FLayout& Layout)
{
	CommandMinimap::Draw(Paint.Canvas, Controller, FVector2D(Layout.Minimap.X, Layout.Minimap.Y) * Layout.Scale, Layout.Minimap.W * Layout.Scale);
}

}
