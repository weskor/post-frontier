#pragma once

#include "CoreMinimal.h"
#include "Rules/EconomyPolicy.h"

// The Team panel (ui.md surface 3): the gift flow, its amounts, the texts of every refusal and the
// log window. Rules only: the controller holds one FFlow, the HUD and the authority read these.
namespace TeamPanelPolicy
{
enum class EResource : uint8
{
	Power,
	Data
};

// A team has at most four commanders, so the panel lists at most three teammates.
inline constexpr int32 MaxTeammates = 3;
// Three fixed amounts per resource, then ALL (the whole balance).
inline constexpr int32 PresetCount = 4;
inline constexpr int32 AllPreset = 3;
// The gift log shows this many rows, newest first; the arrow buttons scroll the rest.
inline constexpr int32 LogRows = 3;
inline constexpr int32 PowerStep = 10;
inline constexpr int32 DataStep = 5;

// The flow's whole state. Teammate is a commander slot, not a row, so it survives a row that moves or leaves.
struct FFlow
{
	bool bOpen = false;
	int32 Teammate = INDEX_NONE;
	EResource Resource = EResource::Power;
	int32 Amount = 100;
	// Entries hidden above the first visible log row; 0 shows the newest.
	int32 LogScroll = 0;
};

const TCHAR* ResourceName(EResource Resource);
// Preset 0 to 2 are 50 / 100 / 200 Power or 10 / 25 / 50 Data; AllPreset is Balance.
int32 PresetAmount(EResource Resource, int32 Preset, int32 Balance);
// The amount a resource starts at: the second preset.
int32 DefaultAmount(EResource Resource);
int32 StepSize(EResource Resource);

void Toggle(FFlow& Flow);
// Opens the panel with Slot chosen; the resource and amount stay as they were.
void OpenFor(FFlow& Flow, int32 Slot);
void SelectTeammate(FFlow& Flow, int32 Slot);
// Changing the resource restarts at its default amount; choosing the current one changes nothing.
void SelectResource(FFlow& Flow, EResource Resource);
void ApplyPreset(FFlow& Flow, int32 Preset, int32 Balance);
// Direction is +1 or -1. The result stays between 0 and Balance, so a stepped amount is always affordable.
void StepAmount(FFlow& Flow, int32 Direction, int32 Balance);
// Direction -1 moves toward newer entries, +1 toward older; the window stays inside Entries.
void ScrollLog(FFlow& Flow, int32 Direction, int32 Entries);
// Brings LogScroll back inside the window after the log changed.
void ClampLog(FFlow& Flow, int32 Entries);
// Index into an oldest-first log of the entry on visible row Row, or INDEX_NONE past the ends.
int32 LogEntryIndex(int32 Entries, int32 Scroll, int32 Row);

enum class ESendVerdict : uint8
{
	Ok,
	BattleOver,
	NoTeammate,
	TeammateLeft,
	NoAmount,
	Insufficient
};

struct FSendInput
{
	bool bBattleLive = true;
	int32 Teammate = INDEX_NONE;
	// The chosen teammate is still a commander of this team in this battle.
	bool bTeammatePresent = false;
	EResource Resource = EResource::Power;
	int32 Amount = 0;
	int32 Balance = 0;
};

// The first reason Send is refused, in the order the panel explains them.
ESendVerdict Verdict(const FSendInput& In);
// "C2 left the team", "Pick an amount above 0", "Not enough Power: you have 340".
void AppendReason(FStringBuilderBase& Out, ESendVerdict Verdict, const FSendInput& In);
// "SEND 100 Power to C2", or "SEND" before a teammate is chosen.
void AppendSendLabel(FStringBuilderBase& Out, const FFlow& Flow);
// "C2" for commander slot 1.
void AppendCommander(FStringBuilderBase& Out, int32 Slot);

// A rate as the rows print it: "+2/s", and one decimal only when the rate is fractional.
void AppendRate(FStringBuilderBase& Out, double PerSecond);
// "pool +6/s Power · +2/s Data · split 3 ways".
void AppendPool(FStringBuilderBase& Out, double PowerPerSecond, double DataPerSecond, int32 Ways);
// "FORTIFY ready" or "FORTIFY 0:47": the commander's cooldown display.
void AppendFortifyChip(FStringBuilderBase& Out, float ReadyAt, float Now);

// One accepted gift as the panel reads the replicated log.
struct FLogEntry
{
	int32 Sender = INDEX_NONE;
	int32 Recipient = INDEX_NONE;
	EResource Resource = EResource::Power;
	int32 Amount = 0;
	float Time = 0.f;
};
// The log window never holds more than the replicated log keeps.
using FLogBuffer = TArray<FLogEntry, TInlineAllocator<EconomyPolicy::GiftLogLimit>>;
// "C2 -> C1   100 Power", the arrow being U+2192.
void AppendLogText(FStringBuilderBase& Out, const FLogEntry& Entry);
// A gift to Self that arrived after SeenThrough: the opener's gold dot.
bool HasUnseenGift(TConstArrayView<FLogEntry> Log, int32 Self, float SeenThrough);
// The newest entry time, or Fallback for an empty log.
float LatestTime(TConstArrayView<FLogEntry> Log, float Fallback);
}
