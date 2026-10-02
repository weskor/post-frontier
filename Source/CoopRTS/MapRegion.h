#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "MapRegion.generated.h"

class ACapturePoint;

UENUM(BlueprintType)
enum class ERegionRole : uint8
{
	Main, Natural, Reward, Tactical
};

UCLASS()
class COOPRTS_API AMapRegion : public AActor
{
	GENERATED_BODY()
public:
	AMapRegion();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	bool Contains(const FVector& WorldLocation) const;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	int32 RegionIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	FText DisplayName;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	ERegionRole RegionRole = ERegionRole::Tactical;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	int32 HomeTeam = -1;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<FVector2D> Polygon;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	TArray<int32> Neighbours;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	TObjectPtr<ACapturePoint> Anchor;
};
