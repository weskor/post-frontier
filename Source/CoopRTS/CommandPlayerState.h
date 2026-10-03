#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "Rules/GameplayConstants.h"
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
	virtual void CopyProperties(APlayerState* NewPlayerState) override;
	static constexpr int32 InitialResources = GameplayConstants::StartingResources;
	// Per-match slot, independent of the shared friendly TeamIndex.
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Commander")
	int32 CommanderIndex = -1;

	UPROPERTY(ReplicatedUsing = OnRep_TeamIndex, BlueprintReadOnly, Category = "Commander")
	int32 TeamIndex = 0;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Economy")
	int32 Resources = InitialResources;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Doctrine")
	EArmyDoctrine Doctrine = EArmyDoctrine::None;
	// Carried human PlayerStates retain only their slot; match economy and doctrine reset.
	void ResetForNewMatch();

	int32 GetIncomePerSecond() const;
	bool TrySpend(int32 Cost);
	void AddResources(int32 Amount);
private:
	UFUNCTION()
	void OnRep_TeamIndex();

	friend class ACommandBuilding;
	// Only a paid workshop purchase may commit research.
	bool TryChooseDoctrine(EArmyDoctrine Choice);
};
