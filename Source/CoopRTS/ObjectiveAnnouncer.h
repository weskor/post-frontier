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
	UPROPERTY()
	FString TargetForceOwnerName;
};

// Non-owning chronological view over the replicated ring; no event copies.
struct FObjectiveEventView
{
	const TArray<FObjectiveEvent>& Storage;
	int32 FirstIndex;
	int32 Num() const { return Storage.Num(); }
	bool IsEmpty() const { return Storage.IsEmpty(); }
	const FObjectiveEvent& operator[](int32 Index) const
	{
		check(Index >= 0 && Index < Num());
		return Storage[(FirstIndex + Index) % Num()];
	}
	const FObjectiveEvent& Last() const { return (*this)[Num() - 1]; }
	struct FIterator
	{
		const FObjectiveEventView* View;
		int32 Index;
		const FObjectiveEvent& operator*() const { return (*View)[Index]; }
		void operator++() { ++Index; }
		bool operator!=(const FIterator& Other) const { return Index != Other.Index; }
	};
	FIterator begin() const { return { this, 0 }; }
	FIterator end() const { return { this, Num() }; }
};

UCLASS()
class COOPRTS_API UObjectiveAnnouncer : public UActorComponent
{
	GENERATED_BODY()
public:
	UObjectiveAnnouncer();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	FObjectiveEventView GetEvents() const { return { Events, OldestEvent }; }
	static constexpr int32 HistoryLimit = 64;
	static constexpr float FeedLifetime = 8.f;
	static constexpr float FadeSeconds = 2.f;
	static UObjectiveAnnouncer* Get(const UObject* Context);
	static FObjectiveForce DescribeForce(const AArmyUnit* Unit);
	void Raise(FName Id, int32 AffectedTeam, const FVector& Location, const TArray<FObjectiveForce>& Forces, int32 DamageTier = 0);
	void RaiseFromUnit(FName Id, const AActor* AffectedStructure, int32 AffectedTeam, const FVector& Location,
		const AArmyUnit* Unit, int32 DamageTier = 0);
private:
	UPROPERTY(ReplicatedUsing = OnRep_Events)
	TArray<FObjectiveEvent> Events;
	AnnouncerPolicy::FThrottle Throttle;
	int32 NextSequence = 1;
	int32 OldestEvent = 0;
	UFUNCTION()
	void OnRep_Events();
	void AppendEvent(const ACommandGameState& State, FName Id, int32 AffectedTeam, const FVector& Location,
		TConstArrayView<FObjectiveForce> Forces, int32 DamageTier, float ServerTime);
	// Live speech is timestamped separately from history; late joiners see
	// retained events without replaying their old voice lines.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastAnnounce(FName Id, float ServerTime);
};
