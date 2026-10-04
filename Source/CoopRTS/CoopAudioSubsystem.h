#pragma once

#include "CoreMinimal.h"
#include "Content/UnitDefinition.h"
#include "Engine/World.h"
#include "GameFramework/SaveGame.h"
#include "Rules/AnnouncerSpeechQueue.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "CoopAudioSubsystem.generated.h"

class UAudioComponent;
class USoundAttenuation;
class USoundClass;
class USoundMix;
class USoundWave;
class UReverbEffect;

UENUM()
enum class ECoopAudioEvent : uint8
{
	Attack,
	Impact,
	Death,
	Place,
	ConstructLoop,
	Complete,
	Cancel,
	Destroyed,
	Deploy,
	Research,
	HQDestroyed,
	Notify,
	CaptureTick,
	SectorCaptured,
	SectorLost,
	Click,
	Select,
	Front,
	Reject,
	Hover,
	Pulse,
	ShieldBreak
};

USTRUCT()
struct FCoopAudioVariants
{
	GENERATED_BODY()
	UPROPERTY()
	TArray<TObjectPtr<USoundWave>> Waves;
	int32 LastVariant = INDEX_NONE;
};

UCLASS()
class UCoopAudioSettings : public USaveGame
{
	GENERATED_BODY()
public:
	UPROPERTY(SaveGame)
	float MasterVolume = 1.f;
};

// Playback is local. RepNotify callers own transition/counter deduplication.
UCLASS()
class COOPRTS_API UCoopAudioSubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()
public:
	UCoopAudioSubsystem();
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	virtual UWorld* GetWorld() const override;
	static UCoopAudioSubsystem* Get(const UObject* WorldContext);

	void PlayUI(FName Event);
	void SetMasterVolume(float Volume);
	float GetMasterVolume() const { return MasterVolume; }
	void PlayUnit(ECoopAudioEvent Event, int32 Team, EUnitRole Role, const FVector& Location, AActor* Owner = nullptr);
	void PlayStructure(ECoopAudioEvent Event, int32 Team, const FVector& Location, AActor* Owner = nullptr);
	void StartConstruction(AActor* Owner, int32 Team);
	void StopConstruction(AActor* Owner);
	void PlayCapture(ECoopAudioEvent Event, const FVector& Location, int32 Milestone = 0);
	void PlayOutcome(bool bVictory);
	void PlayAnnouncer(FName Id, float ServerTime);

private:
	UPROPERTY()
	TMap<int32, FCoopAudioVariants> Sounds;
	UPROPERTY()
	TObjectPtr<USoundClass> MasterClass;
	UPROPERTY()
	TObjectPtr<USoundMix> MasterMix;
	UPROPERTY()
	TObjectPtr<USoundAttenuation> WorldAttenuation;
	UPROPERTY()
	TObjectPtr<USoundWave> AmbienceWave;
	UPROPERTY()
	TObjectPtr<UReverbEffect> WorldReverb;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AmbienceComponent;
	UPROPERTY()
	TMap<FName, TObjectPtr<USoundWave>> AnnouncerSounds;
	UPROPERTY(Transient)
	TObjectPtr<UAudioComponent> AnnouncerComponent;
	AnnouncerSpeechQueue::FQueue AnnouncerQueue;
	TWeakObjectPtr<UWorld> AnnouncerWorld;
	UPROPERTY()
	TObjectPtr<UCoopAudioSettings> Settings;
	float MasterVolume = 1.f;
	FRandomStream Random;
	TWeakObjectPtr<UWorld> MixWorld;
	TMap<TWeakObjectPtr<AActor>, TWeakObjectPtr<UAudioComponent>> ConstructionLoops;
	FDelegateHandle ListenerHandle;
	FDelegateHandle CleanupHandle;
	FDelegateHandle TearDownHandle;
	TWeakObjectPtr<UWorld> AmbienceWorld;
	bool bAmbienceStopping = false;
	// Counts the cue specs asked for; the library is complete when everything asked for loaded.
	int32 ExpectedCues = 0;
	int32 ExpectedWaves = 0;

	static int32 CueKey(ECoopAudioEvent Event, int32 Team, int32 Role);
	void LoadMixAssets();
	void LoadCue(int32 Team, int32 Role, const TCHAR* Folder, ECoopAudioEvent Event, const TCHAR* CueName, int32 Count);
	void LoadCueLibrary();
	void LoadAnnouncerLibrary();
	USoundWave* Choose(ECoopAudioEvent Event, int32 Team, int32 Role, int32 Variant = INDEX_NONE);
	UAudioComponent* Play(ECoopAudioEvent Event, int32 Team, int32 Role, const FVector& Location,
		bool bUI, AActor* LoopOwner = nullptr, int32 Variant = INDEX_NONE);
	void ApplyMasterMix(UWorld* World);
	void UpdateListener(UWorld* World, ELevelTick TickType, float DeltaSeconds);
	void CleanupWorld(UWorld* World, bool bSessionEnded, bool bCleanupResources);
	void StopAllConstruction();
	void UpdateAmbience(UWorld* World);
	void StopAmbience();
	void TearDownWorld(UWorld* World);
	void StartNextAnnouncer();
	void AnnouncerFinished(UAudioComponent* Component);
	void StopAnnouncer();
};
