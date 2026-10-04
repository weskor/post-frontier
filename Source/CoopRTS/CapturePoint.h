#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Rules/GameplayConstants.h"
#include "CapturePoint.generated.h"

class UStaticMeshComponent;

UENUM(BlueprintType)
enum class ECaptureSiteKind : uint8
{
	Resource,
	Reinforcement
};

UCLASS()
class COOPRTS_API ACapturePoint : public AActor
{
	GENERATED_BODY()
public:
	ACapturePoint();
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	// A zero-time step refreshes occupancy without advancing capture.
	void AdvanceCapture(float Seconds);
	static constexpr float CaptureRadius = GameplayConstants::CaptureRadius;

	UPROPERTY(ReplicatedUsing = OnRep_Capture, BlueprintReadOnly, Category = "Territory")
	int32 ControllingTeam = -1;
	UPROPERTY(ReplicatedUsing = OnRep_Capture, BlueprintReadOnly, Category = "Territory")
	float CaptureProgress = 0.f;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	bool bFriendlyPresent = false;
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Territory")
	bool bEnemyPresent = false;
	virtual void BeginPlay() override;
	// Set on the placed level actor; SiteIndex orders ACommandGameState::CaptureSites.
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	ECaptureSiteKind SiteKind = ECaptureSiteKind::Resource;
	UPROPERTY(EditAnywhere, Replicated, BlueprintReadOnly, Category = "Territory")
	int32 SiteIndex = 0;
private:
	// The region this point anchors is Fortified: capture progress holds for both sides.
	bool IsFortifyFrozen() const;
	UPROPERTY(VisibleAnywhere)
	TObjectPtr<UStaticMeshComponent> Marker;
	float CaptureElapsed = 0.f;
	float LastAudioCaptureProgress = 0.f;
	int32 LastAudioControllingTeam = -1;
	bool bCaptureAudioInitialized = false;
	UFUNCTION()
	void OnRep_Capture();
};
