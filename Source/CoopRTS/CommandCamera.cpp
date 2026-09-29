#include "CommandCamera.h"

#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"

namespace
{
	constexpr double ArenaHalfExtent = 4500.0;
	constexpr float MinArmLength = 700.0f;
	constexpr float MaxArmLength = 4500.0f;
}

ACommandCamera::ACommandCamera()
{
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = true;
	bOnlyRelevantToOwner = true;
	SetReplicateMovement(false);
	AutoPossessAI = EAutoPossessAI::Disabled;
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("CameraRoot"));

	SpringArm = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArm"));
	SpringArm->SetupAttachment(RootComponent);
	SpringArm->TargetArmLength = 2400.0f;
	SpringArm->SetRelativeRotation(FRotator(-60.0, 0.0, 0.0));
	SpringArm->bDoCollisionTest = false;
	SpringArm->bUsePawnControlRotation = false;
	SpringArm->bInheritPitch = false;
	SpringArm->bInheritYaw = false;
	SpringArm->bInheritRoll = false;
	SpringArm->bEnableCameraLag = false;
	SpringArm->bEnableCameraRotationLag = false;

	Camera = CreateDefaultSubobject<UCameraComponent>(TEXT("Camera"));
	Camera->SetupAttachment(SpringArm, USpringArmComponent::SocketName);
	Camera->ProjectionMode = ECameraProjectionMode::Perspective;
	Camera->FieldOfView = 60.0f;
	Camera->bUsePawnControlRotation = false;
}

void ACommandCamera::Pan(FVector2D Axis, float DeltaSeconds)
{
	if (!IsLocallyControlled() || Axis.IsNearlyZero()) return;
	const FVector2D Direction = Axis.GetClampedToMaxSize(1.0);
	const double Distance = SpringArm->TargetArmLength * 1.1 * DeltaSeconds;
	FocusOn(GetActorLocation() + FVector(Direction.X, Direction.Y, 0.0) * Distance);
}

void ACommandCamera::Drag(FVector2D PixelDelta)
{
	if (!IsLocallyControlled()) return;
	const APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || PixelDelta.IsNearlyZero())
	{
		return;
	}

	float MouseX = 0.0f;
	float MouseY = 0.0f;
	if (!PlayerController->GetMousePosition(MouseX, MouseY))
	{
		int32 Width = 0;
		int32 Height = 0;
		PlayerController->GetViewportSize(Width, Height);
		MouseX = Width * 0.5f;
		MouseY = Height * 0.5f;
	}

	FVector PreviousOrigin;
	FVector PreviousDirection;
	FVector CurrentOrigin;
	FVector CurrentDirection;
	if (!PlayerController->DeprojectScreenPositionToWorld(
		MouseX - PixelDelta.X, MouseY - PixelDelta.Y, PreviousOrigin, PreviousDirection)
		|| !PlayerController->DeprojectScreenPositionToWorld(
			MouseX, MouseY, CurrentOrigin, CurrentDirection))
	{
		return;
	}

	// Intersect both cursor rays with the map, keeping the grabbed ground point
	// under the pointer without approximating perspective, camera yaw, or zoom.
	if (PreviousDirection.Z >= -UE_SMALL_NUMBER || CurrentDirection.Z >= -UE_SMALL_NUMBER)
	{
		return;
	}

	const FVector PreviousGround = PreviousOrigin - PreviousDirection * (PreviousOrigin.Z / PreviousDirection.Z);
	const FVector CurrentGround = CurrentOrigin - CurrentDirection * (CurrentOrigin.Z / CurrentDirection.Z);
	FocusOn(GetActorLocation() + PreviousGround - CurrentGround);
}

void ACommandCamera::Zoom(float Axis)
{
	if (!IsLocallyControlled()) return;
	SpringArm->TargetArmLength = FMath::Clamp(
		SpringArm->TargetArmLength * FMath::Pow(1.15f, -Axis), MinArmLength, MaxArmLength);
}

void ACommandCamera::FocusOn(FVector Location)
{
	if (!IsLocallyControlled()) return;
	Location.X = FMath::Clamp(Location.X, -ArenaHalfExtent, ArenaHalfExtent);
	Location.Y = FMath::Clamp(Location.Y, -ArenaHalfExtent, ArenaHalfExtent);
	Location.Z = 0.0;
	SetActorLocation(Location);
}
