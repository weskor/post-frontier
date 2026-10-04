#include "PressureVerification.h"
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Engine/World.h"
#include "Json.h"

namespace PressureVerification
{
using namespace CoopRTSNetworkVerification::Probe;

bool Apply(UWorld& World, const TSharedPtr<FJsonObject>& Request, FString& Error)
{
	FString Action;
	Request->TryGetStringField(TEXT("action"), Action);
	if (Action != TEXT("pressureStun"))
		return false;
	ACommandPlayerController* Controller = LocalController(&World);
	ACommandGameState* State = World.GetGameState<ACommandGameState>();
	const double Seconds = Request->GetNumberField(TEXT("seconds"));
	if (!Controller || !State)
		Error = TEXT("pressure fixture world unavailable");
	else if (!bAuthorityFixtures || World.GetNetMode() != NM_ListenServer || !World.GetAuthGameMode())
		Error = TEXT("pressure fixture requires opted-in authority host");
	else if (Seconds <= 0. || Seconds > 60.)
		Error = TEXT("pressure stun seconds out of bounds");
	else
	{
		int32 Stunned = 0;
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->IsAlive() && Building->IsComplete()
				&& Building->OwningPlayerState == Controller->GetPlayerState<ACommandPlayerState>())
			{
				Building->ApplyStun(static_cast<float>(Seconds));
				++Stunned;
			}
		if (Stunned == 0)
			Error = TEXT("pressure stun fixture found no finished building of the local commander");
	}
	return true;
}
}
#endif
