// One-shot and looped cue playback: component creation, construction loops, UI and outcome cues.
#include "CoopAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "CoopAudioInternal.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundWave.h"

using namespace CoopAudio;

UAudioComponent* UCoopAudioSubsystem::Play(ECoopAudioEvent Event, int32 Team, int32 Role,
	const FVector& Location, bool bUI, AActor* LoopOwner, int32 Variant)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
		return nullptr;
	USoundWave* Wave = Choose(Event, Team, Role, Variant);
	if (!Wave)
		return nullptr;
	if (MixWorld != World)
		ApplyMasterMix(World);
	AActor* ComponentOwner = LoopOwner ? LoopOwner : World->GetWorldSettings();
	UAudioComponent* Component = NewObject<UAudioComponent>(ComponentOwner);
	Component->bAutoDestroy = true;
	Component->bStopWhenOwnerDestroyed = LoopOwner != nullptr;
	Component->bAllowSpatialization = !bUI;
	Component->bIsUISound = bUI;
	Component->AttenuationSettings = bUI ? nullptr : WorldAttenuation.Get();
	Component->SetSound(Wave);
	Component->SetWorldLocation(Location);
	Component->RegisterComponentWithWorld(World);
	const bool bWeapon = Event == ECoopAudioEvent::Attack;
	Component->SetPitchMultiplier(bWeapon ? Random.FRandRange(.97f, 1.03f) : 1.f);
	Component->Play();
	return Component;
}

void UCoopAudioSubsystem::PlayUnit(ECoopAudioEvent Event, int32 Team, EUnitRole Role,
	const FVector& Location, AActor* Owner)
{
	if (Event != ECoopAudioEvent::Attack && Event != ECoopAudioEvent::Impact && Event != ECoopAudioEvent::Death)
		return;
	Play(Event, Team, static_cast<int32>(Role), Location, false);
}

void UCoopAudioSubsystem::PlayStructure(ECoopAudioEvent Event, int32 Team, const FVector& Location, AActor* Owner)
{
	if (Event == ECoopAudioEvent::Research && Team == 5)
		Event = ECoopAudioEvent::Notify;
	Play(Event, Team, StructureRole, Location, false);
}

void UCoopAudioSubsystem::StartConstruction(AActor* Owner, int32 Team)
{
	if (!IsValid(Owner) || ConstructionLoops.Contains(Owner))
		return;
	if (UAudioComponent* Component = Play(ECoopAudioEvent::ConstructLoop, Team, StructureRole, Owner->GetActorLocation(), false, Owner))
		ConstructionLoops.Add(Owner, Component);
}

void UCoopAudioSubsystem::StopConstruction(AActor* Owner)
{
	TWeakObjectPtr<UAudioComponent> Component;
	if (ConstructionLoops.RemoveAndCopyValue(Owner, Component))
		if (Component.IsValid())
			Component->Stop();
}

void UCoopAudioSubsystem::StopAllConstruction()
{
	for (const auto& Entry : ConstructionLoops)
		if (Entry.Value.IsValid())
			Entry.Value->Stop();
	ConstructionLoops.Reset();
}

void UCoopAudioSubsystem::PlayCapture(ECoopAudioEvent Event, const FVector& Location, int32 Milestone)
{
	if (Event != ECoopAudioEvent::CaptureTick && Event != ECoopAudioEvent::SectorCaptured && Event != ECoopAudioEvent::SectorLost)
		return;
	Play(Event, 0, UIRole, Location, Event != ECoopAudioEvent::CaptureTick, nullptr,
		Event == ECoopAudioEvent::CaptureTick ? Milestone : INDEX_NONE);
}

void UCoopAudioSubsystem::PlayOutcome(bool bVictory)
{
	StopAllConstruction();
	StopAmbience();
	// The library has no music stings yet: use its distinct completed/power-cut feedback.
	Play(bVictory ? ECoopAudioEvent::SectorCaptured : ECoopAudioEvent::SectorLost, 0, UIRole, FVector::ZeroVector, true);
}

void UCoopAudioSubsystem::PlayUI(FName Event)
{
	static const FName Click(TEXT("Click")), Select(TEXT("Select")), Front(TEXT("Front")), Reject(TEXT("Reject")), Research(TEXT("Research"));
	ECoopAudioEvent Cue;
	if (Event == Click)
		Cue = ECoopAudioEvent::Click;
	else if (Event == Select)
		Cue = ECoopAudioEvent::Select;
	else if (Event == Front)
		Cue = ECoopAudioEvent::Front;
	else if (Event == Reject)
		Cue = ECoopAudioEvent::Reject;
	else if (Event == Research)
		Cue = ECoopAudioEvent::Research;
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Unsupported UI audio event %s"), *Event.ToString());
		return;
	}
	Play(Cue, 0, Cue == ECoopAudioEvent::Research ? StructureRole : UIRole, FVector::ZeroVector, true);
}
