#include "DepositSite.h"

#include "CommandBuilding.h"
#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"
#include "Rules/EconomyPolicy.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "SimulationSettings.h"
#endif

ADepositSite::ADepositSite()
{
	bReplicates = true;
	bAlwaysRelevant = true;
	SetRootComponent(CreateDefaultSubobject<USceneComponent>(TEXT("Root")));
}

void ADepositSite::BeginPlay()
{
	Super::BeginPlay();
	if (HasAuthority())
	{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
		const FSimulationSettings& Settings = FSimulationSettings::ForWorld(GetWorld());
		Remaining = bRich ? Settings.RichAmount : Settings.NormalAmount;
#else
		Remaining = bRich ? EconomyPolicy::RichDepositAmount : EconomyPolicy::NormalDepositAmount;
#endif
	}
}

int32 ADepositSite::RatePerSecond() const
{
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
	const FSimulationSettings& Settings = FSimulationSettings::ForWorld(GetWorld());
	return bRich ? Settings.RichRate : Settings.NormalRate;
#else
	return bRich ? EconomyPolicy::RichDepositRate : EconomyPolicy::NormalDepositRate;
#endif
}

void ADepositSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADepositSite, RegionIndex);
	DOREPLIFETIME(ADepositSite, bRich);
	DOREPLIFETIME(ADepositSite, Remaining);
	DOREPLIFETIME(ADepositSite, Extractor);
}
