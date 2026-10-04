// Match ambience bed and world reverb lifecycle.
#include "CoopAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "CommandGameState.h"
#include "CoopAudioInternal.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/ReverbEffect.h"
#include "Sound/SoundWave.h"

using namespace CoopAudio;

void UCoopAudioSubsystem::UpdateAmbience(UWorld* World)
{
	const ACommandGameState* State = World->GetGameState<ACommandGameState>();
	const bool bInMatch = State && State->HasActorBegunPlay() && State->MatchResult == EMatchResult::Ongoing
		&& !World->bIsTearingDown && !GetGameInstance()->GetLocalPlayers().IsEmpty();
	if (!bInMatch)
	{
		if (AmbienceWorld == World)
			StopAmbience();
		return;
	}
	if (AmbienceWorld != World)
	{
		StopAmbience();
		// Let an outgoing travel fade finish before creating the next bed; never stack.
		if (IsValid(AmbienceComponent.Get()) && AmbienceComponent->IsPlaying())
			return;
		AmbienceComponent = nullptr;
		AmbienceWorld = World;
		bAmbienceStopping = false;
		if (WorldReverb)
			UGameplayStatics::ActivateReverbEffect(World, WorldReverb, ReverbTag, 1.f, 1.f, 1.f);
		if (AmbienceWave)
		{
			// Persistent only so leave/travel can finish its fade; still owned by this subsystem.
			AmbienceComponent = UGameplayStatics::CreateSound2D(World, AmbienceWave, 1.f, 1.f, 0.f, nullptr, true, true);
			if (AmbienceComponent)
				AmbienceComponent->FadeIn(2.f, 1.f);
		}
	}
}

void UCoopAudioSubsystem::StopAmbience()
{
	if (bAmbienceStopping)
		return;
	bAmbienceStopping = true;
	if (IsValid(AmbienceComponent.Get()))
		AmbienceComponent->FadeOut(1.f, 0.f);
	if (UWorld* World = AmbienceWorld.Get())
		UGameplayStatics::DeactivateReverbEffect(World, ReverbTag);
}
