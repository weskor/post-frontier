#include "DepositSite.h"

#include "CommandBuilding.h"
#include "Components/SceneComponent.h"
#include "Net/UnrealNetwork.h"
#include "SimulationSettings.h"

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
		const FSimulationSettings& Settings = FSimulationSettings::ForWorld(GetWorld());
		Remaining = bRich ? Settings.RichAmount : Settings.NormalAmount;
	}
}

int32 ADepositSite::RatePerSecond() const
{
	const FSimulationSettings& Settings = FSimulationSettings::ForWorld(GetWorld());
	return bRich ? Settings.RichRate : Settings.NormalRate;
}

void ADepositSite::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ADepositSite, RegionIndex);
	DOREPLIFETIME(ADepositSite, bRich);
	DOREPLIFETIME(ADepositSite, Remaining);
	DOREPLIFETIME(ADepositSite, Extractor);
}
