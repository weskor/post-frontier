#include "CommandPlayerController.h"

#include "Engine/GameViewportClient.h"
#include "Engine/World.h"
#include "UnrealClient.h"

FVector2D ACommandPlayerController::GetEdgePanAxis() const
{
	const UGameViewportClient* Client = GetWorld()->GetGameViewport();
	const FViewport* Viewport = Client ? Client->Viewport : nullptr;
	float X, Y;
	if (!Viewport || !Viewport->HasFocus() || !Viewport->IsForegroundWindow() || !GetMousePosition(X, Y))
		return FVector2D::ZeroVector;
	const FIntPoint Size = Viewport->GetSizeXY();
	if (X < 0.f || Y < 0.f || X >= Size.X || Y >= Size.Y)
		return FVector2D::ZeroVector;
	constexpr float Margin = 8.f;
	return FVector2D(Y <= Margin ? 1.f : Y >= Size.Y - Margin ? -1.f
															  : 0.f,
		X <= Margin ? -1.f : X >= Size.X - Margin ? 1.f
												  : 0.f);
}
