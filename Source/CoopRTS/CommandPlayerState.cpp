#include "CommandPlayerState.h"

#include "CommandGameState.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"

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
	if (!HasAuthority()) return;
	Resources = InitialResources;
	Doctrine = EArmyDoctrine::None;
	ForceNetUpdate();
}

bool ACommandPlayerState::TryChooseDoctrine(EArmyDoctrine Choice)
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	if (!HasAuthority() || !State || State->MatchResult != EMatchResult::Ongoing
		|| Doctrine != EArmyDoctrine::None
		|| (Choice != EArmyDoctrine::SiegeOptics && Choice != EArmyDoctrine::FieldRepairs
			&& Choice != EArmyDoctrine::EntrenchedFrontline)) return false;
	Doctrine = Choice;
	ForceNetUpdate();
	return true;
}

int32 ACommandPlayerState::GetIncomePerSecond() const
{
	const ACommandGameState* State = GetWorld() ? GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	return State ? State->GetIncomePerSecond() : ACommandGameState::BaselineIncomePerSecond;
}

bool ACommandPlayerState::TrySpend(int32 Cost)
{
	if (!HasAuthority() || Cost <= 0 || Resources < Cost) return false;
	Resources -= Cost;
	ForceNetUpdate();
	return true;
}

void ACommandPlayerState::AddResources(int32 Amount)
{
	if (!HasAuthority() || Amount <= 0) return;
	Resources = static_cast<int32>(FMath::Min<int64>(MAX_int32, static_cast<int64>(Resources) + Amount));
	ForceNetUpdate();
}

void ACommandPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(ACommandPlayerState, Resources);
	DOREPLIFETIME(ACommandPlayerState, Doctrine);
	DOREPLIFETIME(ACommandPlayerState, CommanderIndex);
}
