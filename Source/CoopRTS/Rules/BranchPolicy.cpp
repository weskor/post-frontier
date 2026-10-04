#include "Rules/BranchPolicy.h"

namespace BranchPolicy
{
FDecision Evaluate(const FInput& In)
{
	FDecision Decision;
	if (!In.bBattleLive)
		Decision.Verdict = EVerdict::NoBattle;
	else if (!In.bOwner)
		Decision.Verdict = EVerdict::NotOwner;
	else if (!In.bProducer)
		Decision.Verdict = EVerdict::NotProducer;
	else if (!In.bBuilt)
		Decision.Verdict = EVerdict::NotBuilt;
	else if (!In.bTypeLocked)
		Decision.Verdict = EVerdict::TypeNotLocked;
	else if (!In.bBranchExists)
		Decision.Verdict = EVerdict::NoBranch;
	else if (In.Phase == EBranchPhase::Upgrading)
		Decision.Verdict = EVerdict::Upgrading;
	else if (In.Phase == EBranchPhase::Done)
		Decision.Verdict = EVerdict::AlreadyBought;
	else if (In.bStunned)
		Decision.Verdict = EVerdict::Stunned;
	else if (In.Power < PowerCost || In.Data < DataCost)
	{
		Decision.Verdict = EVerdict::NeedResources;
		Decision.PowerShort = FMath::Max(0, PowerCost - In.Power);
		Decision.DataShort = FMath::Max(0, DataCost - In.Data);
	}
	else
		Decision.Verdict = EVerdict::Accepted;
	return Decision;
}

void AppendReason(FStringBuilderBase& Out, const FDecision& Decision)
{
	switch (Decision.Verdict)
	{
	case EVerdict::Accepted:
		Out << TEXT("Ready to upgrade");
		break;
	case EVerdict::NoBattle:
		Out << TEXT("No live battle");
		break;
	case EVerdict::NotOwner:
		Out << TEXT("Not your building");
		break;
	case EVerdict::NotProducer:
		Out << TEXT("Only production buildings upgrade");
		break;
	case EVerdict::NotBuilt:
		Out << TEXT("Finish the building first");
		break;
	case EVerdict::TypeNotLocked:
		Out << TEXT("Lock a type first");
		break;
	case EVerdict::NoBranch:
		Out << TEXT("No branch for this type");
		break;
	case EVerdict::Upgrading:
		Out << TEXT("Already upgrading");
		break;
	case EVerdict::AlreadyBought:
		Out << TEXT("Already upgraded this battle");
		break;
	case EVerdict::Stunned:
		Out << TEXT("Stunned: wait for the stun to end");
		break;
	case EVerdict::NeedResources:
		if (Decision.PowerShort > 0 && Decision.DataShort > 0)
			Out.Appendf(TEXT("Need %d more Power and %d more Data"), Decision.PowerShort, Decision.DataShort);
		else if (Decision.DataShort > 0)
			Out.Appendf(TEXT("Need %d more Data"), Decision.DataShort);
		else
			Out.Appendf(TEXT("Need %d more Power"), Decision.PowerShort);
		break;
	}
}

bool PausesProduction(EBranchPhase Phase)
{
	return Phase == EBranchPhase::Upgrading;
}

FUpgradeStep Advance(float Progress, float DeltaSeconds, bool bStunned)
{
	FUpgradeStep Step{ FMath::Clamp(Progress, 0.f, UpgradeSeconds), false };
	if (!bStunned && FMath::IsFinite(DeltaSeconds) && DeltaSeconds > 0.f)
		Step.Progress = FMath::Min(UpgradeSeconds, Step.Progress + DeltaSeconds);
	Step.bCompleted = Step.Progress >= UpgradeSeconds;
	return Step;
}

int32 NextRefit(TConstArrayView<FMember> Living, int32 BaseIndex)
{
	int32 Next = INDEX_NONE;
	for (const FMember& Member : Living)
		if (Member.UnitIndex == BaseIndex && Member.Slot >= 0 && (Next == INDEX_NONE || Member.Slot < Next))
			Next = Member.Slot;
	return Next;
}

FRefitProgress RefitProgress(TConstArrayView<FMember> Living, int32 BranchIndex)
{
	FRefitProgress Progress;
	for (const FMember& Member : Living)
	{
		++Progress.Living;
		Progress.Branched += Member.UnitIndex == BranchIndex;
	}
	return Progress;
}

int32 ScaleDurability(int32 Current, int32 OldMax, int32 NewMax)
{
	if (Current <= 0 || OldMax <= 0 || NewMax <= 0)
		return FMath::Max(0, FMath::Min(Current, NewMax));
	const int64 Scaled = (static_cast<int64>(Current) * NewMax + OldMax / 2) / OldMax;
	return static_cast<int32>(FMath::Clamp<int64>(Scaled, 1, NewMax));
}

void AppendPrice(FStringBuilderBase& Out)
{
	Out.Appendf(TEXT("%d Power + %d Data \u00B7 %d s"), PowerCost, DataCost, FMath::RoundToInt(UpgradeSeconds));
}

void AppendButtonText(FStringBuilderBase& Out, FStringView BranchName, FStringView Summary)
{
	Out << FString(BranchName).ToUpper() << TEXT(" ") << Summary << TEXT(" \u00B7 ");
	AppendPrice(Out);
}

void AppendUpgradeText(FStringBuilderBase& Out, FStringView BranchName, float Progress)
{
	Out << TEXT("Upgrading to ") << BranchName;
	Out.Appendf(TEXT(" %d / %d s"), FMath::FloorToInt(FMath::Clamp(Progress, 0.f, UpgradeSeconds)), FMath::RoundToInt(UpgradeSeconds));
}

void AppendDoneText(FStringBuilderBase& Out, FStringView BranchName, FStringView Summary)
{
	Out << TEXT("\u2713 ") << FString(BranchName).ToUpper() << TEXT(" ") << Summary;
}

void AppendRefitChip(FStringBuilderBase& Out, const FRefitProgress& Progress)
{
	Out.Appendf(TEXT("REFIT %d/%d"), Progress.Branched, Progress.Living);
}

void AppendRefitLine(FStringBuilderBase& Out, const FRefitProgress& Progress, FStringView BranchName)
{
	Out.Appendf(TEXT("Refit %d/%d \u2192 "), Progress.Branched, Progress.Living);
	Out << BranchName << TEXT(" \u00B7 cut-off members keep old form");
}
}
