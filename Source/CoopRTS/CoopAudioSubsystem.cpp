#include "CoopAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "CoopAudioInternal.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "Sound/ReverbEffect.h"
#include "Rules/AnnouncerPolicy.h"

using namespace CoopAudio;

namespace
{
constexpr TCHAR SettingsSlot[] = TEXT("CoopAudioSettings");
}

UWorld* UCoopAudioSubsystem::GetWorld() const
{
	const UGameInstance* Instance = GetGameInstance();
	return Instance ? Instance->GetWorld() : nullptr;
}

bool UCoopAudioSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && !IsRunningDedicatedServer() && Super::ShouldCreateSubsystem(Outer);
}

void UCoopAudioSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
	// Native subsystem instances do not inherit this constructor-populated CDO library.
	// Copy references once; variant history belongs to this instance, never the CDO.
	const UCoopAudioSubsystem* Defaults = GetDefault<UCoopAudioSubsystem>();
	Sounds = Defaults->Sounds;
	AnnouncerSounds = Defaults->AnnouncerSounds;
	MasterClass = Defaults->MasterClass;
	MasterMix = Defaults->MasterMix;
	WorldAttenuation = Defaults->WorldAttenuation;
	AmbienceWave = Defaults->AmbienceWave;
	WorldReverb = Defaults->WorldReverb;
	int32 LoadedWaves = 0;
	for (TPair<int32, FCoopAudioVariants>& Cue : Sounds)
	{
		Cue.Value.LastVariant = INDEX_NONE;
		for (const TObjectPtr<USoundWave>& Wave : Cue.Value.Waves)
		{
			if (IsValid(Wave.Get()))
				++LoadedWaves;
		}
	}
	const bool bMasterClassLoaded = IsValid(MasterClass.Get());
	const bool bMasterMixLoaded = IsValid(MasterMix.Get());
	const bool bAttenuationLoaded = IsValid(WorldAttenuation.Get());
	const bool bAmbienceLoaded = IsValid(AmbienceWave.Get());
	const bool bReverbLoaded = IsValid(WorldReverb.Get());
	LoadedWaves += bAmbienceLoaded ? 1 : 0;
	UE_LOG(LogTemp, Display,
		TEXT("CoopAudio library initialized: cues=%d/42 waves=%d/111 announcer=%d/%d master_class=%d master_mix=%d world_attenuation=%d ambience=%d world_reverb=%d"),
		Sounds.Num(), LoadedWaves, AnnouncerSounds.Num(), AnnouncerPolicy::Definitions().Num(),
		bMasterClassLoaded, bMasterMixLoaded, bAttenuationLoaded, bAmbienceLoaded, bReverbLoaded);
	if (Sounds.Num() != 42 || LoadedWaves != 111 || AnnouncerSounds.Num() != AnnouncerPolicy::Definitions().Num()
		|| !bMasterClassLoaded || !bMasterMixLoaded || !bAttenuationLoaded
		|| !bAmbienceLoaded || !bReverbLoaded)
	{
		UE_LOG(LogTemp, Error, TEXT("Incomplete CoopAudio runtime library; see initialization counts"));
	}
	Settings = Cast<UCoopAudioSettings>(UGameplayStatics::LoadGameFromSlot(SettingsSlot, 0));
	if (!Settings)
		Settings = NewObject<UCoopAudioSettings>(this);
	MasterVolume = FMath::IsFinite(Settings->MasterVolume) ? FMath::Clamp(Settings->MasterVolume, 0.f, 1.f) : 1.f;
	Random.Initialize(FPlatformTime::Cycles());
	ListenerHandle = FWorldDelegates::OnWorldPostActorTick.AddUObject(this, &UCoopAudioSubsystem::UpdateListener);
	CleanupHandle = FWorldDelegates::OnWorldCleanup.AddUObject(this, &UCoopAudioSubsystem::CleanupWorld);
	TearDownHandle = FWorldDelegates::OnWorldBeginTearDown.AddUObject(this, &UCoopAudioSubsystem::TearDownWorld);
}

void UCoopAudioSubsystem::Deinitialize()
{
	FWorldDelegates::OnWorldPostActorTick.Remove(ListenerHandle);
	FWorldDelegates::OnWorldCleanup.Remove(CleanupHandle);
	FWorldDelegates::OnWorldBeginTearDown.Remove(TearDownHandle);
	if (IsValid(AmbienceComponent.Get()))
		AmbienceComponent->Stop();
	AmbienceComponent = nullptr;
	if (UWorld* World = AmbienceWorld.Get())
		UGameplayStatics::DeactivateReverbEffect(World, ReverbTag);
	AmbienceWorld.Reset();
	StopAllConstruction();
	StopAnnouncer();
	if (UWorld* World = MixWorld.Get())
		UGameplayStatics::PopSoundMixModifier(World, MasterMix);
	MixWorld.Reset();
	Super::Deinitialize();
}

UCoopAudioSubsystem* UCoopAudioSubsystem::Get(const UObject* WorldContext)
{
	UWorld* World = WorldContext ? WorldContext->GetWorld() : nullptr;
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer || IsRunningCommandlet())
		return nullptr;
	UGameInstance* Instance = World->GetGameInstance();
	return Instance ? Instance->GetSubsystem<UCoopAudioSubsystem>() : nullptr;
}

void UCoopAudioSubsystem::ApplyMasterMix(UWorld* World)
{
	if (!World || World->GetGameInstance() != GetGameInstance() || World->GetNetMode() == NM_DedicatedServer
		|| !MasterClass || !MasterMix)
		return;
	if (MixWorld != World)
	{
		UGameplayStatics::PushSoundMixModifier(World, MasterMix);
		MixWorld = World;
	}
	UGameplayStatics::SetSoundMixClassOverride(World, MasterMix, MasterClass, MasterVolume, 1.f, 0.f, true);
}

void UCoopAudioSubsystem::SetMasterVolume(float Volume)
{
	if (!FMath::IsFinite(Volume))
		return;
	const float Clamped = FMath::Clamp(Volume, 0.f, 1.f);
	if (FMath::IsNearlyEqual(MasterVolume, Clamped))
		return;
	MasterVolume = Clamped;
	Settings->MasterVolume = MasterVolume;
	if (!UGameplayStatics::SaveGameToSlot(Settings, SettingsSlot, 0))
		UE_LOG(LogTemp, Warning, TEXT("Could not persist CoopAudioSettings master volume"));
	// Sound-class overrides affect already active components as well as new playback.
	ApplyMasterMix(GetWorld());
}

void UCoopAudioSubsystem::UpdateListener(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (!World || World->GetGameInstance() != GetGameInstance() || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
		return;
	if (AnnouncerWorld != World && (AnnouncerComponent != nullptr || !AnnouncerQueue.IsEmpty()))
		StopAnnouncer();
	if (MixWorld != World)
		ApplyMasterMix(World);
	UpdateAmbience(World);
	// One ambience lifecycle guard plus camera/listener geometry; one-shot events stay push-driven.
	for (ULocalPlayer* Player : GetGameInstance()->GetLocalPlayers())
	{
		APlayerController* Controller = Player ? Player->GetPlayerController(World) : nullptr;
		if (!Controller || !Controller->IsLocalController())
			continue;
		FVector View;
		FRotator Rotation;
		Controller->GetPlayerViewPoint(View, Rotation);
		const FVector Direction = Rotation.Vector();
		const FVector Ground = Direction.Z < -UE_SMALL_NUMBER ? View - Direction * (View.Z / Direction.Z) : View;
		Controller->SetAudioListenerOverride(nullptr, Ground, FRotator(0.f, Rotation.Yaw, 0.f));
		Controller->SetAudioListenerAttenuationOverride(nullptr, Ground);
	}
}

void UCoopAudioSubsystem::TearDownWorld(UWorld* World)
{
	if (AmbienceWorld == World)
		StopAmbience();
	if (AnnouncerWorld == World)
		StopAnnouncer();
}

void UCoopAudioSubsystem::CleanupWorld(UWorld* World, bool bSessionEnded, bool bCleanupResources)
{
	if (!World || World->GetGameInstance() != GetGameInstance())
		return;
	if (AnnouncerWorld == World)
		StopAnnouncer();
	if (AmbienceWorld == World)
	{
		StopAmbience();
		AmbienceWorld.Reset();
	}
	for (auto It = ConstructionLoops.CreateIterator(); It; ++It)
		if (!It.Key().IsValid() || It.Key()->GetWorld() == World)
		{
			if (It.Value().IsValid())
				It.Value()->Stop();
			It.RemoveCurrent();
		}
	if (MixWorld == World)
	{
		UGameplayStatics::PopSoundMixModifier(World, MasterMix);
		MixWorld.Reset();
	}
}
