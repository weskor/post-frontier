#include "Rules/FortifyPolicy.h"

namespace FortifyPolicy
{
namespace
{
void AppendClock(FStringBuilderBase& Out, float Seconds)
{
	const int32 Whole = FMath::Max(0, FMath::CeilToInt(Seconds));
	Out.Appendf(TEXT("%d:%02d"), Whole / 60, Whole % 60);
}
}

bool IsActive(const FRegionState& Region, float Now)
{
	return Region.Team >= 0 && Now < Region.ExpiresAt;
}

FDecision Evaluate(const FCastInput& In)
{
	FDecision Decision;
	if (!In.bBattleLive)
		Decision.Verdict = EVerdict::NoBattle;
	else if (!In.bCommander)
		Decision.Verdict = EVerdict::NotCommander;
	else if (!In.bRegionExists)
		Decision.Verdict = EVerdict::NoRegion;
	else if (In.RegionController < 0)
		Decision.Verdict = EVerdict::Neutral;
	else if (In.RegionController != In.CasterTeam)
		Decision.Verdict = EVerdict::HeldByEnemy;
	else if (In.Now < In.ReadyAt)
	{
		Decision.Verdict = EVerdict::Cooldown;
		Decision.CooldownLeft = In.ReadyAt - In.Now;
	}
	else if (In.Data < DataCost)
	{
		Decision.Verdict = EVerdict::NeedData;
		Decision.DataShort = DataCost - FMath::Max(0, In.Data);
	}
	else
		Decision.Verdict = EVerdict::Accepted;
	// The refresh warning is information for any verdict: the cursor shows it for a region that is Fortified already.
	Decision.bRefresh = IsActive(In.Region, In.Now) && In.Region.Team == In.CasterTeam;
	Decision.RefreshLeft = Decision.bRefresh ? In.Region.ExpiresAt - In.Now : 0.f;
	return Decision;
}

FRegionState Cast(int32 Team, float Now)
{
	return { Team, Now + DurationSeconds };
}

float CooldownEnd(float Now)
{
	return Now + CooldownSeconds;
}

EEnd Review(const FRegionState& Region, int32 RegionController, float Now)
{
	if (Region.Team < 0)
		return EEnd::None;
	if (Now >= Region.ExpiresAt)
		return EEnd::Expired;
	return RegionController == Region.Team ? EEnd::None : EEnd::RegionLost;
}

float IncomingFor(const FRegionState& Region, int32 VictimTeam, float Now)
{
	return IsActive(Region, Now) && Region.Team == VictimTeam ? IncomingMultiplier : 1.f;
}

bool FreezesCapture(const FRegionState& Region, float Now)
{
	return IsActive(Region, Now);
}

float DefenceFor(const FRegionState& Region, int32 PlannerTeam, float Now)
{
	return IsActive(Region, Now) && Region.Team != PlannerTeam ? JevDefenceMultiplier : 1.f;
}

float SecondsLeft(const FRegionState& Region, float Now)
{
	return IsActive(Region, Now) ? Region.ExpiresAt - Now : 0.f;
}

void AppendReason(FStringBuilderBase& Out, const FDecision& Decision, FStringView RegionName)
{
	switch (Decision.Verdict)
	{
	case EVerdict::NoBattle:
		Out << TEXT("No live battle");
		break;
	case EVerdict::NotCommander:
		Out << TEXT("Only a commander can Fortify");
		break;
	case EVerdict::NoRegion:
		Out << TEXT("Pick a region");
		break;
	case EVerdict::Neutral:
		Out << RegionName << TEXT(" is neutral");
		break;
	case EVerdict::HeldByEnemy:
		Out << RegionName << TEXT(" is held by JEV");
		break;
	case EVerdict::Cooldown:
		Out << TEXT("Cooldown ");
		AppendClock(Out, Decision.CooldownLeft);
		break;
	case EVerdict::NeedData:
		Out.Appendf(TEXT("Need %d more Data"), Decision.DataShort);
		break;
	case EVerdict::Accepted:
		break;
	}
}

bool IsValidTarget(EVerdict Verdict)
{
	return Verdict == EVerdict::Accepted || Verdict == EVerdict::Cooldown || Verdict == EVerdict::NeedData;
}

FDock Dock(float Now, float ReadyAt, int32 Data)
{
	FDock Result;
	if (Now < ReadyAt)
	{
		Result.State = EDockState::Cooldown;
		Result.CooldownLeft = ReadyAt - Now;
	}
	else if (Data < DataCost)
	{
		Result.State = EDockState::NeedData;
		Result.DataShort = DataCost - FMath::Max(0, Data);
	}
	return Result;
}

void AppendCastFeedText(FStringBuilderBase& Out, int32 CommanderSlot, FStringView RegionName)
{
	Out.Appendf(TEXT("Commander %d fortified "), CommanderSlot + 1);
	Out << RegionName;
}

void AppendEndedFeedText(FStringBuilderBase& Out, FStringView RegionName)
{
	Out << TEXT("Fortify at ") << RegionName << TEXT(" ended: region lost");
}
}
