#include "AbilityCommandComponent.h"
#include "CommandGameState.h"
#include "CommandService.h"
#include "Engine/World.h"
#include "GameState/GameStateEconomy.h"
#include "MapRegion.h"
#include "Rules/FortifyPolicy.h"

namespace
{
FString RejectionText(const FortifyPolicy::FDecision& Decision, const AMapRegion* Region)
{
	TStringBuilder<96> Text;
	FortifyPolicy::AppendReason(Text, Decision, Region ? Region->DisplayName.ToString() : FString());
	return FString(Text.ToView());
}

ECommandRejection RejectionKind(FortifyPolicy::EVerdict Verdict)
{
	switch (Verdict)
	{
	case FortifyPolicy::EVerdict::NoBattle:
		return ECommandRejection::Unavailable;
	case FortifyPolicy::EVerdict::NotCommander:
		return ECommandRejection::InvalidOwner;
	default:
		return ECommandRejection::InvalidRequest;
	}
}
}

FortifyPolicy::FCastInput UAbilityCommandComponent::MakeFortifyInput(const ACommandGameState& State,
	const ACommandPlayerState* Caster, const AMapRegion* Region)
{
	FortifyPolicy::FCastInput In;
	In.bBattleLive = State.MatchResult == EMatchResult::Ongoing;
	In.bCommander = IsValid(Caster) && FGameStateEconomy::Roster(State).Contains(Caster);
	In.bRegionExists = IsValid(Region) && State.Regions.Contains(Region);
	In.Now = State.GetServerWorldTimeSeconds();
	if (!IsValid(Caster))
		return In;
	In.CasterTeam = Caster->TeamIndex;
	In.ReadyAt = Caster->FortifyReadyAt;
	In.Data = Caster->Data;
	if (In.bRegionExists)
	{
		In.RegionController = State.GetRegionController(Region->RegionIndex);
		In.Region = Region->GetFortify();
	}
	return In;
}

FCommandResult FCommandService::CastFortify(ACommandPlayerState* Caster, AMapRegion* Region)
{
	ACommandGameState* State = IsValid(Caster) && Caster->HasAuthority() && Caster->GetWorld()
		? Caster->GetWorld()->GetGameState<ACommandGameState>()
		: nullptr;
	if (!State)
		return { ECommandRejection::Unavailable, TEXT("No live battle") };
	const FortifyPolicy::FCastInput In = UAbilityCommandComponent::MakeFortifyInput(*State, Caster, Region);
	const FortifyPolicy::FDecision Decision = FortifyPolicy::Evaluate(In);
	if (!Decision.IsAccepted())
		return { RejectionKind(Decision.Verdict), RejectionText(Decision, Region) };
	// The evaluation covered the balance, so the atomic spend cannot fail; a failure still spends nothing and casts nothing.
	if (!Caster->TrySpend(FResourceCost{ 0, FortifyPolicy::DataCost }))
		return { ECommandRejection::InvalidRequest, TEXT("Fortify rejected: not enough Data.") };
	Region->StartFortify(Caster->TeamIndex, Caster->CommanderIndex);
	Caster->FortifyReadyAt = FortifyPolicy::CooldownEnd(In.Now);
	Caster->ForceNetUpdate();
	UAbilityCommandComponent::PostFortifyCast(*Region, *Caster);
	return { ECommandRejection::None,
		FString::Printf(TEXT("%s at %s."), Decision.bRefresh ? TEXT("Fortify refreshed") : TEXT("Fortify cast"), *Region->DisplayName.ToString()) };
}
