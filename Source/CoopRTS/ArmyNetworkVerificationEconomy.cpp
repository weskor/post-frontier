// Host-only probe fixtures for the economy and construction scenarios: wallets, income, deposits,
// placement candidates and extractor/building damage. The caller has already checked the authority switch.
#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ArmyNetworkVerification.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Commands/CommandService.h"
#include "Content/MatchContent.h"
#include "DepositSite.h"
#include "EnemyCommander.h"
#include "Headquarters.h"
#include "Json.h"

namespace CoopRTSNetworkVerification::Probe
{
namespace
{
FString EconomyPlacement(const FProbeRequest& Probe)
{
	ACommandGameState* State = Probe.State;
	bPlacementCandidateValid = false;
	const int32 BuildingIndex = static_cast<int32>(Probe.Request->GetIntegerField(TEXT("kind")));
	if (!IsValid(State->FriendlyHeadquarters))
		return TEXT("friendly HQ unavailable");
	const FVector Center = State->FriendlyHeadquarters->GetActorLocation();
	for (int32 Ring = 0; Ring < 9; ++Ring)
		for (int32 Direction = 0; Direction < 32; ++Direction)
		{
			const float Angle = Direction * PI / 16.f;
			FVector Candidate = Center + FVector(FMath::Cos(Angle), FMath::Sin(Angle), 0.f) * (380.f + Ring * 160.f);
			Candidate.Z = 5.f;
			Candidate = State->ResolveBuildingLocation(BuildingIndex, Candidate);
			FString Reason;
			if (State->ValidateBuildingPlacement(BuildingIndex, 0, Candidate, Reason))
			{
				bPlacementCandidateValid = true;
				PlacementCandidate = Candidate;
				return FString();
			}
		}
	return TEXT("no valid construction candidate");
}

FString EconomyIncomeTick(const FProbeRequest& Probe)
{
	ACommandGameState* State = Probe.State;
	if (!State->bVerificationIncomePaused)
		return TEXT("pause natural income before isolated payment tick");
	State->bVerificationIncomePaused = false;
	State->Tick(2.f);
	State->bVerificationIncomePaused = true;
	return FString();
}

FString EconomyDepositRemaining(const FProbeRequest& Probe)
{
	ACommandGameState* State = Probe.State;
	const int32 DepositIndex = Probe.Request->GetIntegerField(TEXT("deposit"));
	const int32 Remaining = Probe.Request->GetIntegerField(TEXT("remaining"));
	if (!State->Deposits.IsValidIndex(DepositIndex) || !IsValid(State->Deposits[DepositIndex])
		|| Remaining < 0 || Remaining > 3000)
		return TEXT("invalid finite deposit fixture");
	State->Deposits[DepositIndex]->Remaining = Remaining;
	State->Deposits[DepositIndex]->ForceNetUpdate();
	return FString();
}

FString EconomyEnemyExtractor(const FProbeRequest& Probe)
{
	ACommandGameState* State = Probe.State;
	if (!IsValid(State->EnemyCommander) || !State->Content)
		return TEXT("JEV construction fixture unavailable");
	for (ADepositSite* Deposit : State->Deposits)
	{
		if (!IsValid(Deposit) || IsValid(Deposit->Extractor) || State->GetRegionController(Deposit->RegionIndex) != 5)
			continue;
		const int32 BuildingIndex = State->Content->BuildingIndexOf(TEXT("extractor"));
		FString Reason;
		if (!State->ValidateBuildingPlacement(BuildingIndex, 5, Deposit->GetActorLocation(), Reason))
			continue;
		State->EnemyCommander->Resources = 160;
		ACommandBuilding* Extractor = FCommandService::PlaceBuilding(State->EnemyCommander, BuildingIndex, Deposit->GetActorLocation()).Building;
		if (!Extractor)
			return TEXT("paid JEV extractor fixture rejected");
		Extractor->Tick(60.f); // Accelerated construction fixture, not natural completion proof.
		return FString();
	}
	return TEXT("no free legal JEV deposit");
}

// destroyBuilding orphans a barracks' force through the same hostile damage path.
FString EconomyDestroyBuilding(const FProbeRequest& Probe)
{
	ACommandGameState* State = Probe.State;
	const int32 BuildingIndex = Probe.Request->GetIntegerField(TEXT("building"));
	const EBuildingKind Expected = Probe.Action == TEXT("destroyExtractor") ? EBuildingKind::Extractor : EBuildingKind::Barracks;
	if (!State->Buildings.IsValidIndex(BuildingIndex) || !IsValid(State->Buildings[BuildingIndex])
		|| State->Buildings[BuildingIndex]->Kind != Expected)
		return TEXT("building damage fixture unavailable");
	ACommandBuilding* Extractor = State->Buildings[BuildingIndex];
	const int32 HostileTeam = Extractor->TeamIndex == 5 ? 0 : 5;
	ACommandPlayerState* HostileWallet = HostileTeam == 5 ? State->EnemyCommander.Get() : Probe.PC->GetPlayerState<ACommandPlayerState>();
	const FVector Staging = (HostileTeam == 5 ? State->EnemyHeadquarters : State->FriendlyHeadquarters)->GetActorLocation();
	const FTransform Transform(FRotator::ZeroRotator, Staging + FVector(0.f, 0.f, 100.f));
	AArmyGroup* Hostile = Probe.World->SpawnActorDeferred<AArmyGroup>(AArmyGroup::StaticClass(), Transform,
		nullptr, nullptr, ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Hostile)
		return TEXT("hostile extractor damage fixture allocation failed");
	Hostile->Initialize(FArmyGroupSpawn{ HostileTeam, HostileWallet, -1, nullptr, Transform.GetLocation() });
	Hostile->FinishSpawning(Transform);
	if (!Hostile->SpawnUnits())
	{
		Hostile->Destroy();
		return TEXT("hostile extractor damage fixture spawn failed");
	}
	Extractor->ReceiveAttack(Extractor->Health, Hostile->GetUnits()[0]);
	const bool bDestroyed = !IsValid(Extractor) || !Extractor->IsAlive();
	Hostile->Destroy();
	return bDestroyed ? FString() : TEXT("hostile damage did not destroy extractor");
}

FString EconomyFund(const FProbeRequest& Probe)
{
	ACommandPlayerState* Wallet = nullptr;
	for (APlayerState* Player : Probe.State->PlayerArray)
		if (auto* Candidate = Cast<ACommandPlayerState>(Player))
			if (Candidate->CommanderIndex == Probe.Owner)
				Wallet = Candidate;
	if (!Wallet)
		return TEXT("fixture wallet unavailable");
	const int32 Amount = static_cast<int32>(Probe.Request->GetIntegerField(TEXT("amount")));
	if (Amount < 0 || Amount > 1000)
		return TEXT("fixture wallet amount out of bounds");
	Wallet->Resources = Amount;
	Wallet->ForceNetUpdate();
	return FString();
}
}

bool HandleEconomyFixture(const FProbeRequest& Probe, FString& Error)
{
	const FString& Action = Probe.Action;
	if (Action == TEXT("placement"))
		Error = EconomyPlacement(Probe);
	else if (Action == TEXT("income"))
	{
		Probe.State->bVerificationIncomePaused = Probe.Request->GetBoolField(TEXT("paused"));
		Error = FString();
	}
	else if (Action == TEXT("incomeTick"))
		Error = EconomyIncomeTick(Probe);
	else if (Action == TEXT("depositRemaining"))
		Error = EconomyDepositRemaining(Probe);
	else if (Action == TEXT("enemyExtractor"))
		Error = EconomyEnemyExtractor(Probe);
	else if (Action == TEXT("destroyExtractor") || Action == TEXT("destroyBuilding"))
		Error = EconomyDestroyBuilding(Probe);
	else if (Action == TEXT("fund"))
		Error = EconomyFund(Probe);
	else
		return false;
	return true;
}
}
#endif
