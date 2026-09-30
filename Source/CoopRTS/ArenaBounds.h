#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaBounds.generated.h"

// Placed once per level; the single definition of where orders, spawns, placements and the minimap end.
UCLASS()
class COOPRTS_API AArenaBounds : public AActor
{
	GENERATED_BODY()
public:
	AArenaBounds();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// Orders, unit spawns and pathing goals may use the full extent.
	bool ContainsTravel(const FVector& Location) const;
	// Building placement stays PlacementMargin inside the travel extent.
	bool ContainsPlacement(const FVector& Location) const;
	// The level's arena via the replicated GameState reference; null before a match is set up.
	static const AArenaBounds* Find(const UWorld* World);
	// False when the world has no arena yet.
	static bool IsTravelLocation(const UWorld* World, const FVector& Location)
	{
		const AArenaBounds* Arena = Find(World);
		return Arena && Arena->ContainsTravel(Location);
	}
	static constexpr double HalfHeight = 1000.0;

	UPROPERTY(EditAnywhere, Replicated, Category = "Arena")
	FVector2D HalfExtent = FVector2D(4500.0, 4500.0);
	UPROPERTY(EditAnywhere, Replicated, Category = "Arena")
	float PlacementMargin = 100.f;
};
