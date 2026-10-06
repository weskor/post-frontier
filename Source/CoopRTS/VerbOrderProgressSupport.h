#pragma once

#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/World.h"

namespace VerbOrderTests
{
// Four blocking slabs around Center that stay out of navigation: paths still lead through them, so a unit
// walking to a goal inside the ring presses against the wall and stays there. InnerHalf is the free
// half-width around Center, Thickness the wall depth. False when the engine cube is unavailable.
inline bool BuildCage(UWorld& World, const FVector& Center, float InnerHalf, float Thickness)
{
	UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (!Cube)
		return false;
	constexpr float Height = 300.f;
	const float Middle = InnerHalf + Thickness * .5f;
	// Slabs along X span the whole ring so the corners are closed.
	const float Length = 2.f * (InnerHalf + Thickness);
	const FVector Offsets[] = { FVector(Middle, 0.f, 0.f), FVector(-Middle, 0.f, 0.f), FVector(0.f, Middle, 0.f), FVector(0.f, -Middle, 0.f) };
	for (int32 Side = 0; Side < 4; ++Side)
	{
		FActorSpawnParameters Parameters;
		Parameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AStaticMeshActor* Slab = World.SpawnActor<AStaticMeshActor>(Center + Offsets[Side], FRotator::ZeroRotator, Parameters);
		UStaticMeshComponent* Mesh = Slab ? Slab->GetStaticMeshComponent() : nullptr;
		if (!Mesh)
			return false;
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetStaticMesh(Cube);
		Mesh->SetCollisionProfileName(TEXT("BlockAll"));
		Mesh->SetCanEverAffectNavigation(false);
		Slab->SetActorScale3D(Side < 2 ? FVector(Thickness, Length, Height) / 100.f : FVector(Length, Thickness, Height) / 100.f);
	}
	return true;
}
}

#endif
