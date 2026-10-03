#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/AnnouncerPolicy.h"
#include "Sound/SoundWave.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveAssetsTest, "CoopRTS.Objectives.Assets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FObjectiveAssetsTest::RunTest(const FString& Parameters)
{
	for (const AnnouncerPolicy::FDefinition& Definition : AnnouncerPolicy::Definitions())
	{
		const FString Path = FString::Printf(TEXT("/Game/Audio/Announcer/VO_%s.VO_%s"), Definition.Id, Definition.Id);
		USoundWave* Voice = LoadObject<USoundWave>(nullptr, *Path);
		if (TestNotNull(FString::Printf(TEXT("Voice exists for %s"), Definition.Id), Voice))
			TestTrue(FString::Printf(TEXT("Voice for %s has playable duration"), Definition.Id), Voice->GetDuration() > 0.f);
	}
	return true;
}
#endif
