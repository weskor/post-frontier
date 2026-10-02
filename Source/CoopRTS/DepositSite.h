#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "DepositSite.generated.h"

class ACommandBuilding;

UCLASS()
class COOPRTS_API ADepositSite : public AActor
{
	GENERATED_BODY()
public:
	ADepositSite();
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	int32 RatePerSecond() const;

	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Economy")
	int32 RegionIndex = INDEX_NONE;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Economy")
	bool bRich = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Economy")
	int32 Remaining = 0;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Economy")
	TObjectPtr<ACommandBuilding> Extractor;
};
