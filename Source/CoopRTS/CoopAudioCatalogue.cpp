// Cue catalogue: the constructor-time asset library and variant selection.
#include "CoopAudioSubsystem.h"

#include "CoopAudioInternal.h"
#include "Sound/SoundAttenuation.h"
#include "Sound/SoundClass.h"
#include "Sound/SoundMix.h"
#include "Sound/SoundWave.h"
#include "Sound/ReverbEffect.h"
#include "UObject/ConstructorHelpers.h"
#include "Rules/AnnouncerPolicy.h"

using namespace CoopAudio;

namespace
{
struct FCueSpec
{
	ECoopAudioEvent Event;
	const TCHAR* Name;
	int32 Count;
};
}

int32 UCoopAudioSubsystem::CueKey(ECoopAudioEvent Event, int32 Team, int32 Role)
{
	return (Team == 5 ? 10000 : 0) + Role * 100 + static_cast<int32>(Event);
}

UCoopAudioSubsystem::UCoopAudioSubsystem()
{
	// Hard CDO references make all imported waves and mix assets cook dependencies.
	if (!HasAnyFlags(RF_ClassDefaultObject))
		return;
	LoadMixAssets();
	LoadCueLibrary();
	LoadAnnouncerLibrary();
}

void UCoopAudioSubsystem::LoadMixAssets()
{
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
}

void UCoopAudioSubsystem::LoadCue(int32 Team, int32 Role, const TCHAR* Folder, ECoopAudioEvent Event,
	const TCHAR* CueName, int32 Count)
{
	const TCHAR* Faction = Team == 5 ? TEXT("Machine") : TEXT("Human");
	FCoopAudioVariants& Cue = Sounds.Add(CueKey(Event, Team, Role));
	Cue.Waves.Reserve(Count);
	for (int32 Index = 1; Index <= Count; ++Index)
	{
		const FString Name = FString::Printf(TEXT("SW_%s_%s_%s_%02d"), Faction, Folder, CueName, Index);
		const FString Path = FString::Printf(TEXT("/Game/Audio/%s/%s/%s.%s"), Faction, Folder, *Name, *Name);
		ConstructorHelpers::FObjectFinder<USoundWave> Wave(*Path);
		if (Wave.Succeeded())
			Cue.Waves.Add(Wave.Object);
	}
}

void UCoopAudioSubsystem::LoadCueLibrary()
{
	const auto Load = [this](int32 Team, int32 Role, const TCHAR* Folder, const FCueSpec& Spec) {
		LoadCue(Team, Role, Folder, Spec.Event, Spec.Name, Spec.Count);
	};
	const TPair<EUnitRole, const TCHAR*> Roles[] = { { EUnitRole::Frontline, TEXT("Frontline") }, { EUnitRole::Ranged, TEXT("Ranged") },
		{ EUnitRole::Siege, TEXT("Siege") }, { EUnitRole::Assault, TEXT("Lancer") }, { EUnitRole::Support, TEXT("Scrambler") } };
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
		for (const auto& Role : Roles)
		{
			const int32 Key = static_cast<int32>(Role.Key);
			Load(Team, Key, Role.Value, { ECoopAudioEvent::Attack, Role.Key == EUnitRole::Frontline ? TEXT("Attack") : TEXT("Fire"), 4 });
			Load(Team, Key, Role.Value, { ECoopAudioEvent::Impact, TEXT("Impact"), 3 });
			Load(Team, Key, Role.Value, { ECoopAudioEvent::Death, TEXT("Death"), 3 });
		}
		for (const FCueSpec& Spec : Structures)
			Load(Team, StructureRole, TEXT("Structure"), Spec);
		Load(Team, StructureRole, TEXT("Structure"), Team == 5 ? FCueSpec{ ECoopAudioEvent::Notify, TEXT("Notify"), 2 } : FCueSpec{ ECoopAudioEvent::Research, TEXT("Research"), 2 });
		Load(Team, UnitEffectRole, TEXT("Scrambler"), { ECoopAudioEvent::Pulse, TEXT("Pulse"), 3 });
		Load(Team, UnitEffectRole, TEXT("Lancer"), { ECoopAudioEvent::ShieldBreak, TEXT("ShieldBreak"), 3 });
	}
	const FCueSpec UI[] = {
		{ ECoopAudioEvent::Click, TEXT("Click"), 3 }, { ECoopAudioEvent::Select, TEXT("Select"), 2 },
		{ ECoopAudioEvent::Front, TEXT("Front"), 2 }, { ECoopAudioEvent::Reject, TEXT("Reject"), 2 },
		{ ECoopAudioEvent::Hover, TEXT("Hover"), 2 }, { ECoopAudioEvent::CaptureTick, TEXT("CaptureTick"), 3 },
		{ ECoopAudioEvent::SectorCaptured, TEXT("SectorCaptured"), 2 }, { ECoopAudioEvent::SectorLost, TEXT("SectorLost"), 2 }
	};
	for (const FCueSpec& Spec : UI)
		Load(0, UIRole, TEXT("UI"), Spec);
}

void UCoopAudioSubsystem::LoadAnnouncerLibrary()
{
	for (const AnnouncerPolicy::FDefinition& Definition : AnnouncerPolicy::Definitions())
	{
		const FString Name = FString::Printf(TEXT("VO_%s"), Definition.Id);
		const FString Path = FString::Printf(TEXT("/Game/Audio/Announcer/%s.%s"), *Name, *Name);
		ConstructorHelpers::FObjectFinder<USoundWave> Wave(*Path);
		if (Wave.Succeeded())
			AnnouncerSounds.Add(FName(Definition.Id), Wave.Object);
	}
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
