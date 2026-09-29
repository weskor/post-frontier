#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "CommandPlayerState.generated.h"

UENUM(BlueprintType)
enum class EArmyDoctrine : uint8
{
	None,
	SiegeOptics,
	FieldRepairs,
	EntrenchedFrontline
};

UCLASS()
class COOPRTS_API ACommandPlayerState : public APlayerState
{
	GENERATED_BODY()
public:
	ACommandPlayerState();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// Per-match slot, independent of the shared friendly TeamIndex.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Commander")
	int32 CommanderIndex = -1;


	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Economy")
	int32 Resources = 360;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Doctrine")
	EArmyDoctrine Doctrine = EArmyDoctrine::None;

	bool TryChooseDoctrine(EArmyDoctrine Choice);

	int32 GetIncomePerSecond() const;
	bool TrySpend(int32 Cost);
	void AddResources(int32 Amount);
};
