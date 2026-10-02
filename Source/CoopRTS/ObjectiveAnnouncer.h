#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rules/AnnouncerPolicy.h"
#include "ObjectiveAnnouncer.generated.h"

class AArmyUnit;
class ACommandGameState;

USTRUCT()
struct FObjectiveForce
{
	GENERATED_BODY()
	UPROPERTY()
	int32 TeamIndex = -1;
	UPROPERTY()
	int32 CommanderIndex = -1;
	UPROPERTY()
	int32 ForceNumber = 0;
	UPROPERTY()
	int32 UnitIndex = -1;
	UPROPERTY()
	FString PlayerName;
};

USTRUCT()
struct FObjectiveEvent
{
	GENERATED_BODY()
	UPROPERTY()
	int32 Sequence = 0;
	UPROPERTY()
	FName Id;
	UPROPERTY()
	float ServerTime = 0.f;
	UPROPERTY()
	FVector Location = FVector::ZeroVector;
	UPROPERTY()
	int32 RegionIndex = -1;
	UPROPERTY()
	FString RegionName;
	UPROPERTY()
	int32 AffectedTeam = -1;
	UPROPERTY()
	int32 DamageTier = 0;
	UPROPERTY()
	TArray<FObjectiveForce> Forces;
};

UCLASS()
class COOPRTS_API UObjectiveAnnouncer : public UActorComponent
{
	GENERATED_BODY()
public:
	UObjectiveAnnouncer();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	const TArray<FObjectiveEvent>& GetEvents() const { return Events; }
	static constexpr int32 HistoryLimit = 64;
	static constexpr float FeedLifetime = 8.f;
	static constexpr float FadeSeconds = 2.f;
	static UObjectiveAnnouncer* Get(const UObject* Context);
	static FObjectiveForce DescribeForce(const AArmyUnit* Unit);
	void Raise(FName Id, int32 AffectedTeam, const FVector& Location, const TArray<FObjectiveForce>& Forces, int32 DamageTier = 0);
	void RaiseFromUnit(FName Id, int32 AffectedTeam, const FVector& Location, const AArmyUnit* Unit, int32 DamageTier = 0);
private:
	UPROPERTY(Replicated)
	TArray<FObjectiveEvent> Events;
	AnnouncerPolicy::FThrottle Throttle;
	int32 NextSequence = 1;
	void AppendEvent(const ACommandGameState& State, FName Id, int32 AffectedTeam, const FVector& Location,
		TConstArrayView<FObjectiveForce> Forces, int32 DamageTier, float ServerTime);
	// Reliable live delivery never drops state-change speech when the feed fades;
	// replicated history lets late joiners see state without replaying old voice.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnounce(FName Id);
};
