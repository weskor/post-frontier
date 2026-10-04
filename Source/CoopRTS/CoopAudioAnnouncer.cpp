// Announcer speech: world-bound queue consumption and the single voice component.
// Queue ordering, freshness and overflow decisions live in Rules/AnnouncerSpeechQueue.
#include "CoopAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundWave.h"

namespace
{
double AnnouncerServerTime(const UWorld* World)
{
	const AGameStateBase* GameState = World->GetGameState();
	return GameState ? GameState->GetServerWorldTimeSeconds() : World->GetTimeSeconds();
}
}

void UCoopAudioSubsystem::PlayAnnouncer(FName Id, float ServerTime)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer
		|| World->bIsTearingDown || !AnnouncerSounds.Contains(Id))
		return;
	if (AnnouncerWorld != World)
		StopAnnouncer();
	AnnouncerWorld = World;
	const bool bSpeechPlaying = IsValid(AnnouncerComponent.Get()) && AnnouncerComponent->IsPlaying();
	if (!AnnouncerQueue.Enqueue(Id, ServerTime, AnnouncerServerTime(World), bSpeechPlaying))
		return;
	if (!bSpeechPlaying)
		StartNextAnnouncer();
}

void UCoopAudioSubsystem::StartNextAnnouncer()
{
	UWorld* World = AnnouncerWorld.Get();
	if (!World || World->bIsTearingDown)
	{
		StopAnnouncer();
		return;
	}
	AnnouncerSpeechQueue::FLine Line;
	while (AnnouncerQueue.Dequeue(AnnouncerServerTime(World), Line))
	{
		const TObjectPtr<USoundWave>* Found = AnnouncerSounds.Find(Line.Id);
		USoundWave* Wave = Found ? Found->Get() : nullptr;
		if (!IsValid(Wave))
			continue;
		ApplyMasterMix(World);
		if (!IsValid(AnnouncerComponent.Get()))
		{
			AnnouncerComponent = NewObject<UAudioComponent>(World->GetWorldSettings());
			AnnouncerComponent->bAutoDestroy = false;
			AnnouncerComponent->bStopWhenOwnerDestroyed = true;
			AnnouncerComponent->bAllowSpatialization = false;
			AnnouncerComponent->bIsUISound = true;
			AnnouncerComponent->SoundClassOverride = MasterClass;
			AnnouncerComponent->OnAudioFinishedNative.AddUObject(this, &UCoopAudioSubsystem::AnnouncerFinished);
			AnnouncerComponent->RegisterComponentWithWorld(World);
		}
		AnnouncerComponent->SetSound(Wave);
		AnnouncerComponent->Play();
		return;
	}
}

void UCoopAudioSubsystem::AnnouncerFinished(UAudioComponent* Component)
{
	if (Component == AnnouncerComponent.Get())
		StartNextAnnouncer();
}

void UCoopAudioSubsystem::StopAnnouncer()
{
	AnnouncerQueue.Reset();
	if (IsValid(AnnouncerComponent.Get()))
	{
		AnnouncerComponent->OnAudioFinishedNative.RemoveAll(this);
		AnnouncerComponent->Stop();
		AnnouncerComponent->DestroyComponent();
	}
	AnnouncerComponent = nullptr;
	AnnouncerWorld.Reset();
}
