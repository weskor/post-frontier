#pragma once

#include "CoreMinimal.h"

class AArenaBounds;
class UCanvas;
class ACommandPlayerController;

namespace CommandMinimap
{
	// Origin and Size describe the full arena square in viewport pixels: +X up, +Y right.
	void Draw(UCanvas* Canvas, ACommandPlayerController* Controller, FVector2D Origin, float Size);
	// Inclusive square edges within Arena; invalid inputs leave OutWorld unchanged. Successful results have Z = 0.
	bool ScreenToWorld(const AArenaBounds* Arena, FVector2D Position, FVector2D Origin, float Size, FVector& OutWorld);
}
