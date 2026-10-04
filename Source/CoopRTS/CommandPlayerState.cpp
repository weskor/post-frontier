#include "CommandPlayerState.h"

#include "CommandGameState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "Rules/EconomyPolicy.h"

ACommandPlayerState::ACommandPlayerState()
{
	bReplicates = true;
}

void ACommandPlayerState::CopyProperties(APlayerState* NewPlayerState)
{
	Super::CopyProperties(NewPlayerState);
	// Engine seamless travel creates a new controller/PlayerState pair. Keep only
	// the reserved slot; economy and doctrine are fresh per match.
	if (ACommandPlayerState* NewCommander = Cast<ACommandPlayerState>(NewPlayerState))
		NewCommander->CommanderIndex = CommanderIndex;
}

void ACommandPlayerState::ResetForNewMatch()
{
	if (!HasAuthority())
		return;
	Resources = InitialResources;
	Data = 0;
	PowerCarry = 0;
	DataCarry = 0;
	Doctrine = EArmyDoctrine::None;
	FortifyReadyAt = 0.f;
	ForceNetUpdate();
}

bool ACommandPlayerState::TryChooseDoctrine(EArmyDoctrine Choice)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || !State || State->MatchResult != EMatchResult::Ongoing
		|| Doctrine != EArmyDoctrine::None
		|| (Choice != EArmyDoctrine::SiegeOptics && Choice != EArmyDoctrine::FieldRepairs
			&& Choice != EArmyDoctrine::EntrenchedFrontline))
		return false;
	Doctrine = Choice;
	ForceNetUpdate();
	return true;
}

void ACommandPlayerState::OnRep_TeamIndex()
{
	// Client actors register before their initial replicated team arrives.
	if (TeamIndex == 5)
		if (ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr)
			State->RemovePlayerState(this);
}

bool ACommandPlayerState::TrySpend(int32 Cost)
{
	if (!HasAuthority() || !EconomyPolicy::CanAfford(Resources, Cost))
		return false;
	Resources -= Cost;
	ForceNetUpdate();
	return true;
}

bool ACommandPlayerState::TrySpend(const FResourceCost& Cost)
{
	if (!HasAuthority() || !EconomyPolicy::CanAfford(Resources, Data, Cost))
		return false;
	Resources -= Cost.Power;
	Data -= Cost.Data;
	ForceNetUpdate();
	return true;
}

void ACommandPlayerState::AddResources(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
		return;
	Resources = EconomyPolicy::AddResources(Resources, Amount);
	ForceNetUpdate();
}

void ACommandPlayerState::AddData(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0)
		return;
	Data = EconomyPolicy::AddResources(Data, Amount);
	ForceNetUpdate();
}

void ACommandPlayerState::CreditPoolShare(int32 PowerTotal, int32 DataTotal, int32 Recipients)
{
	if (!HasAuthority())
		return;
	const FPoolShare PowerShare = EconomyPolicy::SplitShare(PowerTotal, Recipients, PowerCarry);
	const FPoolShare DataShare = EconomyPolicy::SplitShare(DataTotal, Recipients, DataCarry);
	Resources = EconomyPolicy::AddResources(Resources, PowerShare.Whole);
	Data = EconomyPolicy::AddResources(Data, DataShare.Whole);
	PowerCarry = PowerShare.Carry;
	DataCarry = DataShare.Carry;
	ForceNetUpdate();
}

void ACommandPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACommandPlayerState, Resources);
	DOREPLIFETIME(ACommandPlayerState, Data);
	DOREPLIFETIME(ACommandPlayerState, PowerCarry);
	DOREPLIFETIME(ACommandPlayerState, DataCarry);
	DOREPLIFETIME(ACommandPlayerState, Doctrine);
	DOREPLIFETIME(ACommandPlayerState, CommanderIndex);
	DOREPLIFETIME(ACommandPlayerState, FortifyReadyAt);
	DOREPLIFETIME(ACommandPlayerState, TeamIndex);
}
