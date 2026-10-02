#include "CoopAudioSubsystem.h"

#include "Components/AudioComponent.h"
#include "CommandGameState.h"
#include "Engine/GameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "Sound/ReverbEffect.h"
#include "UObject/ConstructorHelpers.h"
#include "Rules/AnnouncerPolicy.h"
#include "ObjectiveAnnouncer.h"

namespace
{
constexpr int32 StructureRole = 3;
constexpr int32 UIRole = 4;
constexpr TCHAR SettingsSlot[] = TEXT("CoopAudioSettings");
const FName ReverbTag(TEXT("CoopWorldReverb"));
struct FCueSpec
{
	ECoopAudioEvent Event;
	const TCHAR* Name;
	int32 Count;
};
}

int32 UCoopAudioSubsystem::CueKey(ECoopAudioEvent Event, int32 Team, int32 Role)
{
	return (Team == 5 ? 1000 : 0) + Role * 100 + static_cast<int32>(Event);
}

UCoopAudioSubsystem::UCoopAudioSubsystem()
{
	// Hard CDO references make all imported waves and mix assets cook dependencies.
	if (!HasAnyFlags(RF_ClassDefaultObject))
		return;
	ConstructorHelpers::FObjectFinder<USoundClass> Master(TEXT("/Game/Audio/Mix/SC_Master.SC_Master"));
	ConstructorHelpers::FObjectFinder<USoundMix> Mix(TEXT("/Game/Audio/Mix/SM_Master.SM_Master"));
	ConstructorHelpers::FObjectFinder<USoundAttenuation> Attenuation(TEXT("/Game/Audio/Mix/ATT_World.ATT_World"));
	MasterClass = Master.Object;
	MasterMix = Mix.Object;
	WorldAttenuation = Attenuation.Object;
	ConstructorHelpers::FObjectFinder<USoundWave> Bed(TEXT("/Game/Audio/Human/Ambience/SW_Human_Ambience_NightLoop_01.SW_Human_Ambience_NightLoop_01"));
	ConstructorHelpers::FObjectFinder<UReverbEffect> Reverb(TEXT("/Game/Audio/Mix/RE_World.RE_World"));
	AmbienceWave = Bed.Object;
	WorldReverb = Reverb.Object;
	const auto Load = [this](int32 Team, int32 Role, const TCHAR* Folder, const FCueSpec& Spec) {
		const TCHAR* Faction = Team == 5 ? TEXT("Machine") : TEXT("Human");
		FCoopAudioVariants& Cue = Sounds.Add(CueKey(Spec.Event, Team, Role));
		Cue.Waves.Reserve(Spec.Count);
		for (int32 Index = 1; Index <= Spec.Count; ++Index)
		{
			const FString Name = FString::Printf(TEXT("SW_%s_%s_%s_%02d"), Faction, Folder, Spec.Name, Index);
			const FString Path = FString::Printf(TEXT("/Game/Audio/%s/%s/%s.%s"), Faction, Folder, *Name, *Name);
			ConstructorHelpers::FObjectFinder<USoundWave> Wave(*Path);
			if (Wave.Succeeded())
				Cue.Waves.Add(Wave.Object);
		}
	};
	const TCHAR* Roles[] = { TEXT("Frontline"), TEXT("Ranged"), TEXT("Siege") };
	const FCueSpec Structures[] = {
		{ ECoopAudioEvent::Place, TEXT("Place"), 2 },
		{ ECoopAudioEvent::ConstructLoop, TEXT("ConstructLoop"), 2 },
		{ ECoopAudioEvent::Complete, TEXT("Complete"), 2 },
		{ ECoopAudioEvent::Cancel, TEXT("Cancel"), 2 },
		{ ECoopAudioEvent::Destroyed, TEXT("Destroyed"), 3 },
		{ ECoopAudioEvent::Deploy, TEXT("Deploy"), 2 },
		{ ECoopAudioEvent::HQDestroyed, TEXT("HQDestroyed"), 1 }
	};
	for (int32 Team : { 0, 5 })
	{
		for (int32 Role = 0; Role < UE_ARRAY_COUNT(Roles); ++Role)
		{
			Load(Team, Role, Roles[Role], { ECoopAudioEvent::Attack, Role == 0 ? TEXT("Attack") : TEXT("Fire"), 4 });
			Load(Team, Role, Roles[Role], { ECoopAudioEvent::Impact, TEXT("Impact"), 3 });
			Load(Team, Role, Roles[Role], { ECoopAudioEvent::Death, TEXT("Death"), 3 });
		}
		for (const FCueSpec& Spec : Structures)
			Load(Team, StructureRole, TEXT("Structure"), Spec);
		Load(Team, StructureRole, TEXT("Structure"), Team == 5 ? FCueSpec{ ECoopAudioEvent::Notify, TEXT("Notify"), 2 } : FCueSpec{ ECoopAudioEvent::Research, TEXT("Research"), 2 });
	}
	const FCueSpec UI[] = {
		{ ECoopAudioEvent::Click, TEXT("Click"), 3 }, { ECoopAudioEvent::Select, TEXT("Select"), 2 },
		{ ECoopAudioEvent::Front, TEXT("Front"), 2 }, { ECoopAudioEvent::Reject, TEXT("Reject"), 2 },
		{ ECoopAudioEvent::Hover, TEXT("Hover"), 2 }, { ECoopAudioEvent::CaptureTick, TEXT("CaptureTick"), 3 },
		{ ECoopAudioEvent::SectorCaptured, TEXT("SectorCaptured"), 2 }, { ECoopAudioEvent::SectorLost, TEXT("SectorLost"), 2 }
	};
	for (const FCueSpec& Spec : UI)
		Load(0, UIRole, TEXT("UI"), Spec);
	for (const AnnouncerPolicy::FDefinition& Definition : AnnouncerPolicy::Definitions())
	{
		const FString Name = FString::Printf(TEXT("VO_%s"), Definition.Id);
		const FString Path = FString::Printf(TEXT("/Game/Audio/Announcer/%s.%s"), *Name, *Name);
		ConstructorHelpers::FObjectFinder<USoundWave> Wave(*Path);
		if (Wave.Succeeded())
			AnnouncerSounds.Add(FName(Definition.Id), Wave.Object);
	}
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
	AnnouncerQueue.Reserve(UObjectiveAnnouncer::HistoryLimit);
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

USoundWave* UCoopAudioSubsystem::Choose(ECoopAudioEvent Event, int32 Team, int32 Role, int32 Variant)
{
	FCoopAudioVariants* Cue = Sounds.Find(CueKey(Event, Team, Role));
	if (!Cue || Cue->Waves.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("Missing imported audio cue event=%d team=%d role=%d; run Build/ImportAudio.py"),
			static_cast<int32>(Event), Team, Role);
		return nullptr;
	}
	int32 Index = Variant;
	if (Index == INDEX_NONE)
	{
		Index = Cue->Waves.Num() == 1 ? 0 : Cue->LastVariant == INDEX_NONE ? Random.RandRange(0, Cue->Waves.Num() - 1)
																		   : (Cue->LastVariant + 1 + Random.RandRange(0, Cue->Waves.Num() - 2)) % Cue->Waves.Num();
	}
	if (!Cue->Waves.IsValidIndex(Index))
		return nullptr;
	Cue->LastVariant = Index;
	return Cue->Waves[Index];
}

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

void UCoopAudioSubsystem::PlayAnnouncer(FName Id)
{
	UWorld* World = GetWorld();
	if (!World || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer
		|| World->bIsTearingDown || !AnnouncerSounds.Contains(Id))
		return;
	if (AnnouncerWorld.IsValid() && AnnouncerWorld != World)
		StopAnnouncer();
	AnnouncerWorld = World;
	AnnouncerQueue.Add(Id);
	if (!IsValid(AnnouncerComponent.Get()) || !AnnouncerComponent->IsPlaying())
		StartNextAnnouncer();
}

void UCoopAudioSubsystem::StartNextAnnouncer()
{
	UWorld* World = AnnouncerWorld.Get();
	if (!World || World->bIsTearingDown)
		return;
	while (AnnouncerQueueCursor < AnnouncerQueue.Num())
	{
		const TObjectPtr<USoundWave>* Found = AnnouncerSounds.Find(AnnouncerQueue[AnnouncerQueueCursor++]);
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
	AnnouncerQueue.Reset();
	AnnouncerQueueCursor = 0;
}

void UCoopAudioSubsystem::AnnouncerFinished(UAudioComponent* Component)
{
	if (Component == AnnouncerComponent.Get())
		StartNextAnnouncer();
}

void UCoopAudioSubsystem::StopAnnouncer()
{
	AnnouncerQueue.Reset();
	AnnouncerQueueCursor = 0;
	if (IsValid(AnnouncerComponent.Get()))
	{
		AnnouncerComponent->OnAudioFinishedNative.RemoveAll(this);
		AnnouncerComponent->Stop();
		AnnouncerComponent->DestroyComponent();
	}
	AnnouncerComponent = nullptr;
	AnnouncerWorld.Reset();
}

void UCoopAudioSubsystem::UpdateListener(UWorld* World, ELevelTick TickType, float DeltaSeconds)
{
	if (!World || World->GetGameInstance() != GetGameInstance() || !World->IsGameWorld() || World->GetNetMode() == NM_DedicatedServer)
		return;
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
