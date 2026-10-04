#include "Rules/TeamPanelPolicy.h"

#include "Rules/FortifyPolicy.h"

namespace TeamPanelPolicy
{
const TCHAR* ResourceName(EResource Resource)
{
	return Resource == EResource::Power ? TEXT("Power") : TEXT("Data");
}

int32 PresetAmount(EResource Resource, int32 Preset, int32 Balance)
{
	static constexpr int32 Power[AllPreset] = { 50, 100, 200 };
	static constexpr int32 Data[AllPreset] = { 10, 25, 50 };
	if (Preset == AllPreset)
		return FMath::Max(0, Balance);
	if (Preset < 0 || Preset > AllPreset)
		return 0;
	return Resource == EResource::Power ? Power[Preset] : Data[Preset];
}

int32 DefaultAmount(EResource Resource)
{
	return PresetAmount(Resource, 1, 0);
}

int32 StepSize(EResource Resource)
{
	return Resource == EResource::Power ? PowerStep : DataStep;
}

void Toggle(FFlow& Flow)
{
	Flow.bOpen = !Flow.bOpen;
}

void OpenFor(FFlow& Flow, int32 Slot)
{
	Flow.bOpen = true;
	Flow.Teammate = Slot;
}

void SelectTeammate(FFlow& Flow, int32 Slot)
{
	Flow.Teammate = Slot;
}

void SelectResource(FFlow& Flow, EResource Resource)
{
	if (Flow.Resource == Resource)
		return;
	Flow.Resource = Resource;
	Flow.Amount = DefaultAmount(Resource);
}

void ApplyPreset(FFlow& Flow, int32 Preset, int32 Balance)
{
	Flow.Amount = PresetAmount(Flow.Resource, Preset, Balance);
}

void StepAmount(FFlow& Flow, int32 Direction, int32 Balance)
{
	const int64 Next = static_cast<int64>(Flow.Amount) + static_cast<int64>(Direction) * StepSize(Flow.Resource);
	Flow.Amount = static_cast<int32>(FMath::Clamp<int64>(Next, 0, FMath::Max(0, Balance)));
}

void ClampLog(FFlow& Flow, int32 Entries)
{
	Flow.LogScroll = FMath::Clamp(Flow.LogScroll, 0, FMath::Max(0, Entries - LogRows));
}

void ScrollLog(FFlow& Flow, int32 Direction, int32 Entries)
{
	Flow.LogScroll += Direction;
	ClampLog(Flow, Entries);
}

int32 LogEntryIndex(int32 Entries, int32 Scroll, int32 Row)
{
	const int32 Index = Entries - 1 - Scroll - Row;
	return Row >= 0 && Scroll >= 0 && Index >= 0 && Index < Entries ? Index : INDEX_NONE;
}

ESendVerdict Verdict(const FSendInput& In)
{
	if (!In.bBattleLive)
		return ESendVerdict::BattleOver;
	if (In.Teammate == INDEX_NONE)
		return ESendVerdict::NoTeammate;
	if (!In.bTeammatePresent)
		return ESendVerdict::TeammateLeft;
	if (In.Amount <= 0)
		return ESendVerdict::NoAmount;
	return In.Amount > In.Balance ? ESendVerdict::Insufficient : ESendVerdict::Ok;
}

void AppendCommander(FStringBuilderBase& Out, int32 Slot)
{
	Out.Appendf(TEXT("C%d"), Slot + 1);
}

void AppendReason(FStringBuilderBase& Out, ESendVerdict Verdict, const FSendInput& In)
{
	switch (Verdict)
	{
	case ESendVerdict::BattleOver:
		Out << TEXT("Gifting is closed: the battle is over");
		break;
	case ESendVerdict::NoTeammate:
		Out << TEXT("Pick a teammate first");
		break;
	case ESendVerdict::TeammateLeft:
		AppendCommander(Out, In.Teammate);
		Out << TEXT(" left the team");
		break;
	case ESendVerdict::NoAmount:
		Out << TEXT("Pick an amount above 0");
		break;
	case ESendVerdict::Insufficient:
		Out.Appendf(TEXT("Not enough %s: you have %d"), ResourceName(In.Resource), In.Balance);
		break;
	case ESendVerdict::Ok:
		break;
	}
}

void AppendSendLabel(FStringBuilderBase& Out, const FFlow& Flow)
{
	Out << TEXT("SEND");
	if (Flow.Teammate == INDEX_NONE)
		return;
	Out.Appendf(TEXT(" %d %s to "), Flow.Amount, ResourceName(Flow.Resource));
	AppendCommander(Out, Flow.Teammate);
}

void AppendRate(FStringBuilderBase& Out, double PerSecond)
{
	if (FMath::Abs(PerSecond - FMath::RoundToDouble(PerSecond)) < .05)
		Out.Appendf(TEXT("+%d/s"), static_cast<int32>(FMath::RoundToDouble(PerSecond)));
	else
		Out.Appendf(TEXT("+%.1f/s"), PerSecond);
}

void AppendPool(FStringBuilderBase& Out, double PowerPerSecond, double DataPerSecond, int32 Ways)
{
	Out << TEXT("pool ");
	AppendRate(Out, PowerPerSecond);
	Out << TEXT(" Power \u00B7 ");
	AppendRate(Out, DataPerSecond);
	Out.Appendf(TEXT(" Data \u00B7 split %d way%s"), Ways, Ways == 1 ? TEXT("") : TEXT("s"));
}

void AppendFortifyChip(FStringBuilderBase& Out, float ReadyAt, float Now)
{
	if (ReadyAt <= Now)
	{
		Out << TEXT("FORTIFY ready");
		return;
	}
	Out << TEXT("FORTIFY ");
	FortifyPolicy::AppendClock(Out, ReadyAt - Now);
}

void AppendLogText(FStringBuilderBase& Out, const FLogEntry& Entry)
{
	AppendCommander(Out, Entry.Sender);
	Out << TEXT(" \u2192 ");
	AppendCommander(Out, Entry.Recipient);
	Out.Appendf(TEXT("   %d %s"), Entry.Amount, ResourceName(Entry.Resource));
}

bool HasUnseenGift(TConstArrayView<FLogEntry> Log, int32 Self, float SeenThrough)
{
	for (const FLogEntry& Entry : Log)
		if (Entry.Recipient == Self && Entry.Time > SeenThrough)
			return true;
	return false;
}

float LatestTime(TConstArrayView<FLogEntry> Log, float Fallback)
{
	float Latest = Fallback;
	for (const FLogEntry& Entry : Log)
		Latest = FMath::Max(Latest, Entry.Time);
	return Latest;
}
}
