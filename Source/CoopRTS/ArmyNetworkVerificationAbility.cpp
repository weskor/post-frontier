// Probe actions and snapshot for the Fortify scenarios. A cast goes through the local controller's RPC like a
// player's click; the Data and cooldown fixtures run on the authority host only.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/AbilityCommandComponent.h"
#include "Json.h"
#include "MapRegion.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
AMapRegion* FindRegion(const ACommandGameState& State, int32 RegionIndex)
{
	for (AMapRegion* Region : State.Regions)
		if (IsValid(Region) && Region->RegionIndex == RegionIndex)
			return Region;
	return nullptr;
}

ACommandPlayerState* FindCommander(const ACommandGameState& State, int32 CommanderIndex)
{
	for (APlayerState* Player : State.PlayerArray)
		if (auto* Candidate = Cast<ACommandPlayerState>(Player); Candidate && Candidate->CommanderIndex == CommanderIndex)
			return Candidate;
	return nullptr;
}

FString AbilityCast(const FProbeRequest& Probe)
{
	if (!Probe.PC || !Probe.State)
		return TEXT("local controller unavailable");
	AMapRegion* Region = FindRegion(*Probe.State, static_cast<int32>(Probe.Request->GetIntegerField(TEXT("region"))));
	if (!Region)
		return TEXT("region not replicated locally");
	Probe.PC->AbilityCommands->ServerCastFortify(Region);
	return FString();
}

// fortifyFund sets a commander's Data; fortifyCooldown sets the seconds left on its Fortify cooldown.
FString AbilityWallet(const FProbeRequest& Probe)
{
	ACommandPlayerState* Wallet = FindCommander(*Probe.State, Probe.Owner);
	if (!Wallet)
		return TEXT("fixture wallet unavailable");
	if (Probe.Action == TEXT("fortifyFund"))
	{
		const int32 Amount = static_cast<int32>(Probe.Request->GetIntegerField(TEXT("data")));
		if (Amount < 0 || Amount > 1000)
			return TEXT("fixture Data amount out of bounds");
		Wallet->Data = Amount;
	}
	else
	{
		const double Seconds = Probe.Request->GetNumberField(TEXT("seconds"));
		if (Seconds < 0. || Seconds > 600.)
			return TEXT("fixture cooldown out of bounds");
		Wallet->FortifyReadyAt = Probe.State->GetServerWorldTimeSeconds() + static_cast<float>(Seconds);
	}
	Wallet->ForceNetUpdate();
	return FString();
}
}

bool HandleAbilityAction(const FProbeRequest& Probe, FString& Error)
{
	if (Probe.Action != TEXT("fortifyCast"))
		return false;
	Error = AbilityCast(Probe);
	return true;
}

bool HandleAbilityFixture(const FProbeRequest& Probe, FString& Error)
{
	if (Probe.Action != TEXT("fortifyFund") && Probe.Action != TEXT("fortifyCooldown"))
		return false;
	Error = AbilityWallet(Probe);
	return true;
}

void AbilitySnapshot(UWorld* World, const ACommandGameState& State, const TSharedPtr<FJsonObject>& Result)
{
	auto Fortify = Object();
	Number(Fortify, TEXT("serverNow"), State.GetServerWorldTimeSeconds());
	if (const ACommandPlayerController* PC = LocalController(World))
		Fortify->SetBoolField(TEXT("targeting"), PC->IsFortifyTargeting());
	auto Regions = TArray<TSharedPtr<FJsonValue>>();
	for (const AMapRegion* Region : State.Regions)
	{
		if (!IsValid(Region))
			continue;
		auto Entry = Object();
		Number(Entry, TEXT("index"), Region->RegionIndex);
		Number(Entry, TEXT("team"), Region->FortifyTeam);
		Number(Entry, TEXT("caster"), Region->FortifyCaster);
		Number(Entry, TEXT("expiresAt"), Region->FortifyExpiresAt);
		Number(Entry, TEXT("controller"), State.GetRegionController(Region->RegionIndex));
		Regions.Add(MakeShared<FJsonValueObject>(Entry));
	}
	Fortify->SetArrayField(TEXT("regions"), Regions);
	auto Commanders = TArray<TSharedPtr<FJsonValue>>();
	for (const APlayerState* Player : State.PlayerArray)
		if (const auto* Wallet = Cast<ACommandPlayerState>(Player); IsValid(Wallet))
		{
			auto Entry = Object();
			Number(Entry, TEXT("index"), Wallet->CommanderIndex);
			Number(Entry, TEXT("team"), Wallet->TeamIndex);
			Number(Entry, TEXT("data"), Wallet->Data);
			Number(Entry, TEXT("readyAt"), Wallet->FortifyReadyAt);
			Commanders.Add(MakeShared<FJsonValueObject>(Entry));
		}
	Fortify->SetArrayField(TEXT("commanders"), Commanders);
	Result->SetObjectField(TEXT("fortify"), Fortify);
}
}
#endif
