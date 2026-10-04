#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Rules/FortifyPolicy.h"
#include "ObjectiveAnnouncer.h"
#include "AbilityCommandComponent.generated.h"

class AMapRegion;
class ACommandGameState;
class ACommandPlayerState;

// Region abilities (Fortify in step 1b). Owner-only delivery of the team feed rows they raise: they
// stay out of the objective history, so they never reach the Space cycle.
UCLASS()
class COOPRTS_API UAbilityCommandComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UAbilityCommandComponent();
	UFUNCTION(Server, Reliable)
	void ServerCastFortify(AMapRegion* Region);
	UFUNCTION(Client, Reliable)
	void ClientFortifyFeedback(const FString& Message, bool bAccepted);
	UFUNCTION(Client, Reliable)
	void ClientReceiveTeamEvent(const FObjectiveEvent& Event);
	FObjectiveEventView GetEvents() const { return { Events, OldestEvent }; }
	static constexpr float Lifetime = UObjectiveAnnouncer::FeedLifetime;
	// Row sequences count down from here, below every ping sequence, so a row's sequence names its ring.
	static constexpr int32 SequenceBase = -1000000;
	// What the verdict reads, assembled from replicated state so the cursor chip and the authority agree on
	// every peer. Region and Caster may be null.
	static FortifyPolicy::FCastInput MakeFortifyInput(const ACommandGameState& State, const ACommandPlayerState* Caster,
		const AMapRegion* Region);
	static bool IsAbilitySequence(int32 Sequence) { return Sequence <= SequenceBase; }
	void ResetForMatch();

	// Server: tells every other human commander of the caster's team about a cast.
	static void PostFortifyCast(const AMapRegion& Region, const ACommandPlayerState& Caster);
	// Server: tells every human commander of Team that its Fortify ended because the region was lost.
	static void PostFortifyEnded(const AMapRegion& Region, int32 Team);

private:
	void PlayCastVoice(float ServerTime) const;
	UPROPERTY(Transient)
	TArray<FObjectiveEvent> Events;
	int32 OldestEvent = 0;
	int32 NextSequence = SequenceBase;
};
