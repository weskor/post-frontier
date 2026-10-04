#include "Rules/PlanningHudPolicy.h"
namespace PlanningHud
{
void AppendClock(FStringBuilderBase& Out, double Remaining)
{
	const int32 Whole = FMath::CeilToInt(static_cast<float>(FMath::Max(0., Remaining)));
	Out.Appendf(TEXT("PLANNING %d:%02d"), Whole / 60, Whole % 60);
}

void AppendTimeLeft(FStringBuilderBase& Out, double Remaining)
{
	const int32 Whole = FMath::CeilToInt(static_cast<float>(FMath::Max(0., Remaining)));
	Out.Appendf(TEXT("%d:%02d left \u00B7 nothing runs yet"), Whole / 60, Whole % 60);
}

bool IsPulsing(double Remaining)
{
	return Remaining > 0. && Remaining <= PulseSeconds;
}

float PulseOpacity(double Remaining, double Now)
{
	return IsPulsing(Remaining) ? .775f + .225f * FMath::Cos(static_cast<float>(FMath::Fmod(Now, 1.) * 2. * PI)) : 1.f;
}

void AppendReady(FStringBuilderBase& Out, bool bReady, int32 Ready, int32 Humans)
{
	if (bReady)
		Out.Appendf(TEXT("\u2713 READY (%d/%d)  [Enter] Undo"), Ready, Humans);
	else
		Out.Appendf(TEXT("[Enter] READY (%d/%d)"), Ready, Humans);
}

EChip ChipState(bool bReady, bool bKitPlaced)
{
	return bReady ? EChip::Ready : bKitPlaced ? EChip::Placed
											  : EChip::Placing;
}

void AppendChip(FStringBuilderBase& Out, int32 CommanderSlot, EChip State)
{
	Out.Appendf(TEXT("C%d "), CommanderSlot + 1);
	switch (State)
	{
	case EChip::Ready:
		Out << TEXT("\u2713 READY");
		break;
	case EChip::Placed:
		Out << TEXT("PLACED");
		break;
	default:
		Out << TEXT("PLACING");
		break;
	}
}

EEnter Enter(const FEnterInput& In)
{
	if (!In.bPlanning || !In.bHasKit)
		return EEnter::Ignore;
	if (In.bReady)
		return EEnter::Unready;
	return In.bDefaultsNeeded && !In.bConfirmLive ? EEnter::Confirm : EEnter::Ready;
}

bool IsConfirmLive(double Now, double Asked)
{
	return Now >= Asked && Now - Asked <= ConfirmSeconds;
}

void AppendConfirm(FStringBuilderBase& Out, bool bBarracks, bool bRig, FStringView BarracksName, FStringView RigName)
{
	if (bBarracks && bRig)
		Out << BarracksName << TEXT(" and ") << RigName << TEXT(" not placed: default spots. Press Enter again");
	else
		Out << (bBarracks ? BarracksName : RigName) << TEXT(" not placed: default spot. Press Enter again");
}

EKitCard KitCard(bool bPlaced, bool bReady, bool bSiteAvailable)
{
	return bReady ? EKitCard::Locked : bPlaced ? EKitCard::Placed
		: bSiteAvailable                       ? EKitCard::Free
											   : EKitCard::NoDeposit;
}

void AppendKitCard(FStringBuilderBase& Out, EKitCard State)
{
	switch (State)
	{
	case EKitCard::Placed:
		Out << TEXT("PLACED \u00B7 click to move");
		break;
	case EKitCard::NoDeposit:
		Out << TEXT("no free deposit \u00B7 Power back at 0:00");
		break;
	case EKitCard::Locked:
		Out << TEXT("READY \u00B7 locked");
		break;
	default:
		Out << TEXT("FREE KIT \u00B7 place");
		break;
	}
}
}
