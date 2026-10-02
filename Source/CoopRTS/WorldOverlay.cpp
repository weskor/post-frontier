#include "WorldOverlay.h"

#include "Components/InstancedStaticMeshComponent.h"
#include "Containers/StaticArray.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
template <int32 Segments>
const TStaticArray<FVector2D, Segments>& CircleDirections()
{
	static const TStaticArray<FVector2D, Segments> Directions = [] {
		TStaticArray<FVector2D, Segments> Result;
		for (int32 Index = 0; Index < Segments; ++Index)
		{
			const double Angle = Index * (2. * UE_PI / Segments);
			Result[Index] = FVector2D(FMath::Cos(Angle), FMath::Sin(Angle));
		}
		return Result;
	}();
	return Directions;
}
}

AWorldOverlay::AWorldOverlay()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
	SetReplicates(false);
	Cells = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Cells"));
	SetRootComponent(Cells);
	Lines = CreateDefaultSubobject<UInstancedStaticMeshComponent>(TEXT("Lines"));
	Lines->SetupAttachment(Cells);
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Plane(TEXT("/Engine/BasicShapes/Plane.Plane"));
	static ConstructorHelpers::FObjectFinder<UStaticMesh> Cube(TEXT("/Engine/BasicShapes/Cube.Cube"));
	static ConstructorHelpers::FObjectFinder<UMaterialInterface> Material(TEXT("/Game/Materials/Overlay/M_WorldOverlay.M_WorldOverlay"));
	Cells->SetStaticMesh(Plane.Object);
	Lines->SetStaticMesh(Cube.Object);
	for (UInstancedStaticMeshComponent* Mesh : { Cells.Get(), Lines.Get() })
	{
		Mesh->SetMobility(EComponentMobility::Movable);
		Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Mesh->SetGenerateOverlapEvents(false);
		Mesh->SetCanEverAffectNavigation(false);
		Mesh->SetCastShadow(false);
		Mesh->NumCustomDataFloats = 4;
		Mesh->SetMaterial(0, Material.Object);
	}
	Lines->TranslucencySortPriority = Cells->TranslucencySortPriority + 1;
}

bool UWorldOverlaySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	const UWorld* World = Cast<UWorld>(Outer);
	return Super::ShouldCreateSubsystem(Outer) && World && World->IsGameWorld()
		&& World->GetNetMode() != NM_DedicatedServer;
}

AWorldOverlay* UWorldOverlaySubsystem::GetOverlay()
{
	if (!IsValid(Overlay))
	{
		FActorSpawnParameters Params;
		Params.ObjectFlags |= RF_Transient;
		Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		Overlay = GetWorld()->SpawnActor<AWorldOverlay>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	}
	return Overlay;
}

AWorldOverlay* AWorldOverlay::Get(const UObject* Context)
{
	UWorld* World = Context ? Context->GetWorld() : nullptr;
	if (!World || World->GetNetMode() == NM_DedicatedServer)
		return nullptr;
	UWorldOverlaySubsystem* Subsystem = World->GetSubsystem<UWorldOverlaySubsystem>();
	return Subsystem ? Subsystem->GetOverlay() : nullptr;
}

void AWorldOverlay::Cell(const FVector& Center, const FVector2D& HalfSize, FColor Color)
{
	PendingCells.Add({ FTransform(FQuat::Identity, Center, FVector(HalfSize.X * .02, HalfSize.Y * .02, 1.)), FLinearColor(Color) });
}

void AWorldOverlay::Line(const FVector& Start, const FVector& End, FColor Color, float Width)
{
	const FVector Delta = End - Start;
	const double Length = Delta.Size();
	if (Length <= UE_SMALL_NUMBER)
		return;
	PendingLines.Add({ FTransform(Delta.Rotation(), (Start + End) * .5,
						   FVector(Length * .01, Width * .01, Width * .01)),
		FLinearColor(Color) });
}

void AWorldOverlay::Square(const FVector& Center, const FVector2D& HalfSize, FColor Color, float Width)
{
	const FVector Corners[] = {
		Center + FVector(-HalfSize.X, -HalfSize.Y, 0.), Center + FVector(HalfSize.X, -HalfSize.Y, 0.),
		Center + FVector(HalfSize.X, HalfSize.Y, 0.), Center + FVector(-HalfSize.X, HalfSize.Y, 0.)
	};
	for (int32 Index = 0; Index < 4; ++Index)
		Line(Corners[Index], Corners[(Index + 1) % 4], Color, Width);
}

void AWorldOverlay::Ring(const FVector& Center, float Radius, FColor Color)
{
	constexpr int32 Segments = 48;
	FVector Previous = Center + FVector(Radius, 0., 0.);
	for (int32 Index = 1; Index <= Segments; ++Index)
	{
		const FVector2D Direction = CircleDirections<Segments>()[Index % Segments];
		const FVector Next = Center + FVector(Direction.X * Radius, Direction.Y * Radius, 0.);
		Line(Previous, Next, Color, 2.f);
		Previous = Next;
	}
}

void AWorldOverlay::Attack(const FVector& Start, const FVector& End, FColor Color, bool bSiege)
{
	Flashes.Add({ Start, End, Color, bSiege, GetWorld()->GetTimeSeconds() + .32f });
}

void AWorldOverlay::Flush(UInstancedStaticMeshComponent* Mesh, TArray<FInstance>& Pending, TArray<FInstance>& Previous)
{
	bool bChanged = false;
	const int32 AddedInstances = Pending.Num() - Mesh->GetInstanceCount();
	if (AddedInstances > 0)
		Mesh->PreAllocateInstancesMemory(AddedInstances);
	if (AddedInstances < 0)
	{
		if (AddedInstances == -1)
			Mesh->RemoveInstance(Pending.Num());
		else
		{
			RemovalIndices.Reset(-AddedInstances);
			for (int32 Index = Mesh->GetInstanceCount() - 1; Index >= Pending.Num(); --Index)
				RemovalIndices.Add(Index);
			Mesh->RemoveInstances(RemovalIndices, true);
		}
		bChanged = true;
	}
	for (int32 Index = 0; Index < Pending.Num(); ++Index)
	{
		const FInstance& Instance = Pending[Index];
		const bool bExisting = Previous.IsValidIndex(Index);
		if (bExisting && Instance.Transform.Equals(Previous[Index].Transform) && Instance.Color == Previous[Index].Color)
			continue;
		if (bExisting)
			Mesh->UpdateInstanceTransform(Index, Instance.Transform, false, false, true);
		else
			Mesh->AddInstance(Instance.Transform);
		Mesh->SetCustomDataValue(Index, 0, Instance.Color.R);
		Mesh->SetCustomDataValue(Index, 1, Instance.Color.G);
		Mesh->SetCustomDataValue(Index, 2, Instance.Color.B);
		Mesh->SetCustomDataValue(Index, 3, Instance.Color.A);
		bChanged = true;
	}
	if (bChanged)
		Mesh->MarkRenderInstancesDirty();
	// Swap retained buffers rather than allocating or copying every frame.
	Swap(Previous, Pending);
	Pending.Reset();
}

void AWorldOverlay::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	const float Now = GetWorld()->GetTimeSeconds();
	Flashes.RemoveAllSwap([Now](const FFlash& Flash) { return Flash.Expires <= Now; }, EAllowShrinking::No);
	for (const FFlash& Flash : Flashes)
	{
		Line(Flash.Start, Flash.End, Flash.Color, Flash.bSiege ? 6.f : 3.f);
		const float Radius = Flash.bSiege ? 32.f : 17.f;
		// Three great circles keep the hit sphere readable without a separate mesh/material pool.
		for (int32 Axis = 0; Axis < 3; ++Axis)
			for (int32 Segment = 0; Segment < 8; ++Segment)
			{
				auto Point = [&Flash, Radius, Axis](int32 Index) {
					const FVector2D Direction = CircleDirections<8>()[Index % 8];
					const double X = Direction.X * Radius;
					const double Y = Direction.Y * Radius;
					return Flash.End + (Axis == 0 ? FVector(X, Y, 0.) : Axis == 1 ? FVector(X, 0., Y)
																				  : FVector(0., X, Y));
				};
				Line(Point(Segment), Point(Segment + 1), Flash.Color, 2.f);
			}
	}
	Flush(Cells, PendingCells, PreviousCells);
	Flush(Lines, PendingLines, PreviousLines);
}
