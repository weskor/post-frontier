#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "HUD/MapPresentation.h"
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
	void Ring(const FVector& Center, float Radius, FColor Color, float Width = 2.f);
	void Attack(const FVector& Start, const FVector& End, FColor Color, bool bSiege);
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	int32 PendingLineCount() const { return PendingLines.Num(); }
	int32 DrawnLineCount() const;
#endif

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
	// Submissions are one frame's drawing. Whatever was submitted more than a frame before the newest is stale and goes, so a
	// world that does not flush (a missed tick) can never accumulate an unbounded backlog.
	void NoteSubmission();
	void DropStaleSubmissions();
	static constexpr uint64 NoSubmission = TNumericLimits<uint64>::Max();
	uint64 PendingSince = NoSubmission;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Cells;
	UPROPERTY()
	TObjectPtr<UInstancedStaticMeshComponent> Lines;
	TArray<FInstance> PendingCells;
	TArray<FInstance> PendingLines;
	TArray<FInstance> PreviousCells;
	TArray<FInstance> PreviousLines;
	TArray<int32> RemovalIndices;
	TArray<FFlash> Flashes;
	FMapPresentationWorld MapWorld;
};

UCLASS()
class COOPRTS_API UWorldOverlaySubsystem : public UWorldSubsystem
{
	GENERATED_BODY()
public:
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	AWorldOverlay* GetOverlay();
private:
	UPROPERTY()
	TObjectPtr<AWorldOverlay> Overlay;
};
