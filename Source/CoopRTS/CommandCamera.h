#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Pawn.h"
#include "CommandCamera.generated.h"

class UCameraComponent;
class USpringArmComponent;

UCLASS()
class COOPRTS_API ACommandCamera : public APawn
{
	GENERATED_BODY()

public:
	ACommandCamera();

	// Axes are world X/Y; diagonal input is limited to the same pan speed.
	void Pan(FVector2D Axis, float DeltaSeconds);

	// Viewport pixel displacement: positive X right, positive Y down.
	void Drag(FVector2D PixelDelta);

	// Positive input moves closer to the ground.
	void Zoom(float Axis);

	void FocusOn(FVector Location);

	// Motion blur off for this camera's view only (a post-process override, no global state). The frozen planning world
	// keeps its last frame's velocity, so with blur on the whole scene smears until the world runs again.
	void SetMotionBlurSuppressed(bool bSuppressed);
	bool IsMotionBlurSuppressed() const;

private:
	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;
};
