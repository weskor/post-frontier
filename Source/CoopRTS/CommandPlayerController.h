#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "ArmyGroup.h"
#include "CommandPlayerState.h"
#include "CommandPlayerController.generated.h"

class UInputAction;
class UInputMappingContext;

UCLASS()
class COOPRTS_API ACommandPlayerController : public APlayerController
{
	GENERATED_BODY()
public:
	ACommandPlayerController();
	virtual void PlayerTick(float DeltaTime) override;
	AArmyGroup* GetSelectedArmy() const { return SelectedArmy; }
	const FString& GetOrderFeedback() const { return OrderFeedback; }
	const FString& GetDoctrineFeedback() const { return DoctrineFeedback; }

	UFUNCTION(Server, Reliable)
	void ServerIssueOrder(AArmyGroup* Army, EArmyOrder Order, FVector Destination);
	UFUNCTION(Server, Reliable)
	void ServerIssueAttack(AArmyGroup* Army, FVector Destination, AActor* Target);
	UFUNCTION(Client, Reliable)
	void ClientAttackFeedback(bool bAccepted);
	UFUNCTION(Client, Reliable)
	void ClientOrderFeedback(bool bAccepted);
	UFUNCTION(Server, Reliable)
	void ServerReinforce(AArmyGroup* Army);
	UFUNCTION(Client, Reliable)
	void ClientReinforcementFeedback(const FString& Message);
	UFUNCTION(Server, Reliable)
	void ServerRequestRestart();
	UFUNCTION(Server, Reliable)
	void ServerChooseDoctrine(EArmyDoctrine Choice);
	UFUNCTION(Client, Reliable)
	void ClientDoctrineFeedback(const FString& Message);

protected:
	virtual void BeginPlay() override;
	virtual void SetupInputComponent() override;

private:
	UPROPERTY(Transient) TObjectPtr<AArmyGroup> SelectedArmy;
	UPROPERTY(Transient) TObjectPtr<UInputMappingContext> Mapping;
	UPROPERTY(Transient) TArray<TObjectPtr<UInputAction>> Actions;
	FString OrderFeedback;
	FString DoctrineFeedback;
	EArmyDoctrine LastObservedDoctrine = EArmyDoctrine::None;
	// Group and player-state replication may arrive in either order. Resolve a
	// requested slot once, without undoing a later click or camera movement.
	int32 PendingArmyIndex = 0;
	float SelectionRetry = 0.f;
	bool bInitialFocusPending = true;
	FVector2D PreviousDragPosition = FVector2D::ZeroVector;
	bool bDragging = false;
	void PanForward();
	void PanBackward();
	FVector2D PendingPan = FVector2D::ZeroVector;
	void PanLeft();
	void PanRight();
	void ZoomIn();
	void ZoomOut();
	void SelectUnderCursor();
	void MoveUnderCursor();
	void AttackUnderCursor();
	void Hold();
	void Retreat();
	void Reinforce();
	void RequestRestart();
	void ChooseSiegeOptics();
	void ChooseFieldRepairs();
	void ChooseEntrenchedFrontline();
	void ChooseDoctrine(EArmyDoctrine Choice);
	bool IsOwnedArmy(const AArmyGroup* Army) const;
	bool IsCursorOverDoctrinePanel() const;
	bool CanIssueGameplayCommand();
	bool IsMatchTerminal() const;
	void SelectArmyOne();
	void SelectArmyTwo();
	void SelectArmy(int32 ArmyIndex);
	void FocusArmy();
	bool CursorHit(FHitResult& Hit) const;
};
