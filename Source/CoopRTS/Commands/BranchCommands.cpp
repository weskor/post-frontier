#include "BranchCommands.h"
#include "CommandBuilding.h"
#include "CommandGameState.h"
#include "CommandPlayerState.h"
#include "Content/UnitDefinition.h"
#include "Engine/World.h"

namespace
{
ECommandRejection RejectionKind(BranchPolicy::EVerdict Verdict)
{
	switch (Verdict)
	{
	case BranchPolicy::EVerdict::NoBattle:
	case BranchPolicy::EVerdict::NotBuilt:
		return ECommandRejection::Unavailable;
	case BranchPolicy::EVerdict::NotOwner:
		return ECommandRejection::InvalidOwner;
	default:
		return ECommandRejection::InvalidRequest;
	}
}

FCommandResult Rejected(const BranchPolicy::FDecision& Decision)
{
	TStringBuilder<96> Reason;
	BranchPolicy::AppendReason(Reason, Decision);
	return { RejectionKind(Decision.Verdict), FString(Reason.ToView()) };
}
}

BranchPolicy::FInput FBranchCommands::MakeInput(const ACommandBuilding& Building, const ACommandPlayerState* Buyer)
{
	BranchPolicy::FInput In;
	const ACommandGameState* State = Building.GetWorld() ? Building.GetWorld()->GetGameState<ACommandGameState>() : nullptr;
	In.bBattleLive = State && State->MatchResult == EMatchResult::Ongoing;
	In.bOwner = IsValid(Buyer) && Buyer->TeamIndex == 0 && Buyer->CommanderIndex >= 0 && Buyer->CommanderIndex < 5
		&& Building.TeamIndex == Buyer->TeamIndex && Building.OwningPlayerState == Buyer;
	In.bProducer = Building.IsProducer();
	In.bBuilt = Building.IsComplete() && Building.IsAlive();
	In.bTypeLocked = Building.bForceConfigured;
	In.bBranchExists = Building.GetBranchUnitIndex() != INDEX_NONE;
	In.Phase = Building.Branch.Phase;
	In.bStunned = Building.IsStunned();
	if (IsValid(Buyer))
	{
		In.Power = Buyer->Resources;
		In.Data = Buyer->Data;
	}
	return In;
}

FCommandResult FBranchCommands::Purchase(ACommandPlayerState* Buyer, ACommandBuilding* Building)
{
	if (!IsValid(Buyer) || !Buyer->HasAuthority() || !IsValid(Building) || Building->IsActorBeingDestroyed()
		|| Building->GetWorld() != Buyer->GetWorld())
		return { ECommandRejection::Unavailable, TEXT("Upgrade rejected: building unavailable.") };
	const BranchPolicy::FDecision Decision = BranchPolicy::Evaluate(MakeInput(*Building, Buyer));
	if (!Decision.IsAccepted())
		return Rejected(Decision);
	// The verdict covered the wallet, so the atomic spend cannot fail; if it does it spends nothing and starts nothing.
	if (!Buyer->TrySpend(FResourceCost{ BranchPolicy::PowerCost, BranchPolicy::DataCost }))
		return { ECommandRejection::InvalidRequest, TEXT("Upgrade rejected: not enough Power or Data.") };
	Building->StartBranchUpgrade();
	const UArmyUnitDefinition* Branch = Building->GetBranchDefinition();
	return { ECommandRejection::None,
		FString::Printf(TEXT("Upgrading to %s"), Branch ? *Branch->DisplayName.ToString() : TEXT("the branch")), Building };
}
