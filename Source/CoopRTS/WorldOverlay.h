#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Subsystems/WorldSubsystem.h"
#include "WorldOverlay.generated.h"

class UInstancedStaticMeshComponent;

// Frame submissions are consumed after gameplay ticks; attack flashes persist in world time.
UCLASS(NotBlueprintable, Transient)
class COOPRTS_API AWorldOverlay : public AActor
{
	GENERATED_BODY()
public:
	AWorldOverlay();
	static AWorldOverlay* Get(const UObject* Context);
	virtual void Tick(float DeltaSeconds) override;
	void Cell(const FVector& Center, const FVector2D& HalfSize, FColor Color);
	void Line(const FVector& Start, const FVector& End, FColor Color, float Width);
	void Square(const FVector& Center, const FVector2D& HalfSize, FColor Color, float Width);
	void Ring(const FVector& Center, float Radius, FColor Color);
	void Attack(const FVector& Start, const FVector& End, FColor Color, bool bSiege);

private:
	struct FInstance
	{
		FTransform Transform;
		FLinearColor Color;
	};
	struct FFlash
	{
		FVector Start;
		FVector End;
		FColor Color;
		bool bSiege;
		float Expires;
	};
	void Flush(UInstancedStaticMeshComponent* Mesh, TArray<FInstance>& Pending, TArray<FInstance>& Previous);
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Cells;
	UPROPERTY() TObjectPtr<UInstancedStaticMeshComponent> Lines;
	TArray<FInstance> PendingCells;
	TArray<FInstance> PendingLines;
	TArray<FInstance> PreviousCells;
	TArray<FInstance> PreviousLines;
	TArray<int32> RemovalIndices;
	TArray<FFlash> Flashes;
};

UCLASS()
class COOPRTS_API UWorldOverlaySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	AWorldOverlay* GetOverlay();
private:
	UPROPERTY() TObjectPtr<AWorldOverlay> Overlay;
};
