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

private:
	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<USpringArmComponent> SpringArm;

	UPROPERTY(VisibleAnywhere, Category = "Camera")
	TObjectPtr<UCameraComponent> Camera;
};
