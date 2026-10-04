#include "CommandCamera.h"

#include "ArenaBounds.h"
#include "Camera/CameraComponent.h"
#include "Components/SceneComponent.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "GroundHeight.h"

namespace
{
constexpr float MinArmLength = 700.0f;
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
	if (!IsLocallyControlled() || Axis.IsNearlyZero())
		return;
	const FVector2D Direction = Axis.GetClampedToMaxSize(1.0);
	const double Distance = SpringArm->TargetArmLength * 1.1 * DeltaSeconds;
	FocusOn(GetActorLocation() + FVector(Direction.X, Direction.Y, 0.0) * Distance);
}

void ACommandCamera::Drag(FVector2D PixelDelta)
{
	if (!IsLocallyControlled())
		return;
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

	// Intersect both cursor rays with the ground plane under the camera focus (z = 0 on flat ground, 300 on a
	// plateau), keeping the grabbed ground point under the pointer without approximating perspective, yaw, or zoom.
	if (PreviousDirection.Z >= -UE_SMALL_NUMBER || CurrentDirection.Z >= -UE_SMALL_NUMBER)
	{
		return;
	}

	const double PlaneZ = GetActorLocation().Z;
	const FVector PreviousGround = PreviousOrigin - PreviousDirection * ((PreviousOrigin.Z - PlaneZ) / PreviousDirection.Z);
	const FVector CurrentGround = CurrentOrigin - CurrentDirection * ((CurrentOrigin.Z - PlaneZ) / CurrentDirection.Z);
	FocusOn(GetActorLocation() + PreviousGround - CurrentGround);
}

void ACommandCamera::Zoom(float Axis)
{
	const AArenaBounds* Arena = IsLocallyControlled() ? AArenaBounds::Find(GetWorld()) : nullptr;
	if (!Arena)
		return;
	// Fully zoomed out, the arm spans the arena's half extent.
	SpringArm->TargetArmLength = FMath::Clamp(SpringArm->TargetArmLength * FMath::Pow(1.15f, -Axis),
		MinArmLength, static_cast<float>(Arena->HalfExtent.GetMax()));
}

void ACommandCamera::FocusOn(FVector Location)
{
	const AArenaBounds* Arena = IsLocallyControlled() ? AArenaBounds::Find(GetWorld()) : nullptr;
	if (!Arena)
		return;
	Location.X = FMath::Clamp(Location.X, -Arena->HalfExtent.X, Arena->HalfExtent.X);
	Location.Y = FMath::Clamp(Location.Y, -Arena->HalfExtent.Y, Arena->HalfExtent.Y);
	Location.Z = GroundHeight::At(*GetWorld(), Location.X, Location.Y);
	SetActorLocation(Location);
}
