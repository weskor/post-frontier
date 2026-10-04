// Probe actions for the pause, ping and restart scenarios. They go through the owning controller's real RPCs.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyGroup.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/MatchCommandComponent.h"
#include "Commands/PingCommandComponent.h"
#include "Json.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
FString MatchEnginePause(const FProbeRequest& Probe)
{
	if (!Probe.PC)
		return TEXT("local owning controller unavailable");
	Probe.PC->ServerPause();
	return FString();
}

FString MatchPauseOrResume(const FProbeRequest& Probe)
{
	if (!Probe.Own || Probe.Own->CommanderIndex < 0 || !Probe.State)
		return TEXT("local owning controller unavailable");
	if (Probe.Action == TEXT("pause"))
		Probe.PC->MatchCommands->ServerPause();
	else
		Probe.PC->MatchCommands->ServerResume();
	return FString();
}

FString MatchPingAtScreen(const FProbeRequest& Probe, bool bWithoutPlayerState)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	ACommandPlayerController* PC = Probe.PC;
	if (bWithoutPlayerState)
	{
		const uint32 Serial = PC->PingCommands->PingFeedbackSerial;
		PC->SetPlayerState(nullptr);
		const bool bSubmitted = PC->PingAtScreenPosition(FVector2D(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y"))));
		PC->SetPlayerState(Probe.Own);
		return !bSubmitted && PC->PingCommands->PingFeedbackSerial == Serial
			? FString()
			: TEXT("minimap ping before PlayerState arrived submitted a command");
	}
	return PC->PingAtScreenPosition(FVector2D(Request->GetNumberField(TEXT("x")), Request->GetNumberField(TEXT("y"))))
		? FString()
		: TEXT("screen ping placement rejected");
}

FString MatchPing(const FProbeRequest& Probe)
{
	const TSharedPtr<FJsonObject>& Request = Probe.Request;
	if (!Probe.PC || !Probe.Own || Probe.Own->CommanderIndex < 0 || !Probe.PC->PingCommands)
		return TEXT("local owning ping controller unavailable");
	bool bWithoutPlayerState = false;
	Request->TryGetBoolField(TEXT("withoutPlayerState"), bWithoutPlayerState);
	if (Probe.Action == TEXT("pingAtScreenPosition"))
		return MatchPingAtScreen(Probe, bWithoutPlayerState);
	AArmyGroup* Target = nullptr;
	bool bForce = false;
	Request->TryGetBoolField(TEXT("force"), bForce);
	if (bForce)
	{
		Target = FindArmy(Probe.World, Probe.Owner, Probe.Index);
		if (!Target)
			return TEXT("ping target force not replicated locally");
	}
	Probe.PC->PingCommands->ServerPing(FVector(Request->GetNumberField(TEXT("x")),
										   Request->GetNumberField(TEXT("y")), Request->GetNumberField(TEXT("z"))),
		Target);
	return FString();
}

FString MatchRestart(const FProbeRequest& Probe)
{
	if (!Probe.Own || Probe.Own->CommanderIndex < 0)
		return TEXT("local owning controller unavailable");
	Probe.PC->MatchCommands->ServerRequestRestart();
	return FString();
}
}

bool HandleMatchAction(const FProbeRequest& Probe, FString& Error)
{
	if (Probe.Action == TEXT("enginePause"))
		Error = MatchEnginePause(Probe);
	else if (Probe.Action == TEXT("pause") || Probe.Action == TEXT("resume"))
		Error = MatchPauseOrResume(Probe);
	else if (Probe.Action == TEXT("ping") || Probe.Action == TEXT("pingAtScreenPosition"))
		Error = MatchPing(Probe);
	else if (Probe.Action == TEXT("restart"))
		Error = MatchRestart(Probe);
	else
		return false;
	return true;
}
}
#endif
