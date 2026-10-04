#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "Misc/AutomationTest.h"
#include "Rules/ControllerInputPolicy.h"
#include "Rules/TeamPanelPolicy.h"

// Pure rule tests: no world, no actors. The gift flow, amounts, refusal texts, log window and Esc order.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPanelAmountsTest, "CoopRTS.Rules.TeamPanel.Amounts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPanelFlowTest, "CoopRTS.Rules.TeamPanel.Flow",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPanelRefusalTest, "CoopRTS.Rules.TeamPanel.Refusals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPanelTextTest, "CoopRTS.Rules.TeamPanel.Text",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPanelLogTest, "CoopRTS.Rules.TeamPanel.Log",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPanelEscapeTest, "CoopRTS.Rules.TeamPanel.Escape",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

namespace
{
using namespace TeamPanelPolicy;

FString Reason(const FSendInput& In)
{
	TStringBuilder<96> Text;
	AppendReason(Text, Verdict(In), In);
	return FString(Text.ToView());
}

// A Power send of 100 from a 340 balance to a present teammate, slot 1 (C2).
FSendInput Ready()
{
	FSendInput In;
	In.Teammate = 1;
	In.bTeammatePresent = true;
	In.Amount = 100;
	In.Balance = 340;
	return In;
}
}

bool FTeamPanelAmountsTest::RunTest(const FString&)
{
	TestEqual(TEXT("Power presets are 50 / 100 / 200"),
		FString::Printf(TEXT("%d/%d/%d"), PresetAmount(EResource::Power, 0, 0), PresetAmount(EResource::Power, 1, 0),
			PresetAmount(EResource::Power, 2, 0)),
		FString(TEXT("50/100/200")));
	TestEqual(TEXT("Data presets are 10 / 25 / 50"),
		FString::Printf(TEXT("%d/%d/%d"), PresetAmount(EResource::Data, 0, 0), PresetAmount(EResource::Data, 1, 0),
			PresetAmount(EResource::Data, 2, 0)),
		FString(TEXT("10/25/50")));
	TestEqual(TEXT("ALL is the whole balance"), PresetAmount(EResource::Power, AllPreset, 340), 340);
	TestEqual(TEXT("ALL of an empty wallet is zero"), PresetAmount(EResource::Data, AllPreset, 0), 0);
	TestEqual(TEXT("ALL never goes negative"), PresetAmount(EResource::Data, AllPreset, -5), 0);
	TestEqual(TEXT("An unknown preset is zero"), PresetAmount(EResource::Power, 4, 340), 0);
	TestEqual(TEXT("Power starts at its second preset"), DefaultAmount(EResource::Power), 100);
	TestEqual(TEXT("and so does Data"), DefaultAmount(EResource::Data), 25);
	FFlow Flow;
	StepAmount(Flow, 1, 340);
	TestEqual(TEXT("Power steps by 10"), Flow.Amount, 110);
	Flow.Resource = EResource::Data;
	Flow.Amount = 25;
	StepAmount(Flow, 1, 80);
	TestEqual(TEXT("Data steps by 5"), Flow.Amount, 30);
	Flow.Amount = 3;
	StepAmount(Flow, -1, 80);
	TestEqual(TEXT("Stepping down stops at zero"), Flow.Amount, 0);
	StepAmount(Flow, -1, 80);
	TestEqual(TEXT("and stays there"), Flow.Amount, 0);
	Flow.Amount = 78;
	StepAmount(Flow, 1, 80);
	TestEqual(TEXT("Stepping up stops at the balance"), Flow.Amount, 80);
	Flow.Amount = 200;
	StepAmount(Flow, -1, 80);
	TestEqual(TEXT("An amount above the balance steps back down to it"), Flow.Amount, 80);
	StepAmount(Flow, 1, 0);
	TestEqual(TEXT("An empty wallet steps to zero"), Flow.Amount, 0);
	StepAmount(Flow, 1, -9);
	TestEqual(TEXT("A negative balance cannot push the amount below zero"), Flow.Amount, 0);
	return true;
}

bool FTeamPanelFlowTest::RunTest(const FString&)
{
	FFlow Flow;
	TestTrue(TEXT("The panel starts closed with nobody chosen"), !Flow.bOpen && Flow.Teammate == INDEX_NONE);
	Toggle(Flow);
	TestTrue(TEXT("Toggle opens"), Flow.bOpen);
	Toggle(Flow);
	TestFalse(TEXT("and closes"), Flow.bOpen);
	SelectTeammate(Flow, 2);
	SelectResource(Flow, EResource::Data);
	TestTrue(TEXT("Choosing DATA restarts at 25"), Flow.Resource == EResource::Data && Flow.Amount == 25);
	ApplyPreset(Flow, 2, 80);
	TestEqual(TEXT("A preset sets its amount"), Flow.Amount, 50);
	SelectResource(Flow, EResource::Data);
	TestEqual(TEXT("Choosing the current resource keeps the amount"), Flow.Amount, 50);
	ApplyPreset(Flow, AllPreset, 80);
	TestEqual(TEXT("ALL takes the balance"), Flow.Amount, 80);
	SelectResource(Flow, EResource::Power);
	TestTrue(TEXT("Choosing POWER restarts at 100"), Flow.Resource == EResource::Power && Flow.Amount == 100);
	TestEqual(TEXT("and keeps the teammate"), Flow.Teammate, 2);
	FFlow Other;
	OpenFor(Other, 3);
	TestTrue(TEXT("Gift... opens the panel with that teammate chosen"), Other.bOpen && Other.Teammate == 3);
	Other.Amount = 60;
	OpenFor(Other, 1);
	TestTrue(TEXT("and keeps the amount and resource as they were"), Other.Teammate == 1 && Other.Amount == 60 && Other.Resource == EResource::Power);
	return true;
}

bool FTeamPanelRefusalTest::RunTest(const FString&)
{
	FSendInput In = Ready();
	TestEqual(TEXT("A funded send to a present teammate is fine"), Verdict(In), ESendVerdict::Ok);
	TestEqual(TEXT("and prints no reason"), Reason(In), FString());
	In.Amount = 340;
	TestEqual(TEXT("The whole balance is affordable"), Verdict(In), ESendVerdict::Ok);
	In.Amount = 341;
	TestEqual(TEXT("One more than the balance is refused"), Reason(In), FString(TEXT("Not enough Power: you have 340")));
	In.Resource = EResource::Data;
	In.Balance = 20;
	TestEqual(TEXT("with the resource's own name and balance"), Reason(In), FString(TEXT("Not enough Data: you have 20")));
	In = Ready();
	In.Amount = 0;
	TestEqual(TEXT("Zero is refused"), Reason(In), FString(TEXT("Pick an amount above 0")));
	In.Amount = -10;
	TestEqual(TEXT("and so is a negative amount"), Reason(In), FString(TEXT("Pick an amount above 0")));
	In = Ready();
	In.bTeammatePresent = false;
	TestEqual(TEXT("A teammate who left is named"), Reason(In), FString(TEXT("C2 left the team")));
	In.Teammate = INDEX_NONE;
	TestEqual(TEXT("Nobody chosen asks for a teammate"), Reason(In), FString(TEXT("Pick a teammate first")));
	In = Ready();
	In.bBattleLive = false;
	TestEqual(TEXT("After the battle gifting is closed"), Reason(In), FString(TEXT("Gifting is closed: the battle is over")));
	// Order: battle, teammate, presence, amount, funds.
	In = Ready();
	In.bPlanning = true;
	TestEqual(TEXT("Planning says when gifting opens"), Reason(In), FString(TEXT("Gifting opens at 0:00")));
	In.bBattleLive = false;
	In.Teammate = INDEX_NONE;
	In.Amount = 0;
	TestEqual(TEXT("The battle outranks every other reason"), Verdict(In), ESendVerdict::BattleOver);
	In.bBattleLive = true;
	TestEqual(TEXT("then planning"), Verdict(In), ESendVerdict::Planning);
	In.bPlanning = false;
	TestEqual(TEXT("then a missing teammate"), Verdict(In), ESendVerdict::NoTeammate);
	In.Teammate = 1;
	In.bTeammatePresent = false;
	TestEqual(TEXT("then a departed one"), Verdict(In), ESendVerdict::TeammateLeft);
	In.bTeammatePresent = true;
	TestEqual(TEXT("then a zero amount"), Verdict(In), ESendVerdict::NoAmount);
	In.Amount = 500;
	TestEqual(TEXT("then the balance"), Verdict(In), ESendVerdict::Insufficient);
	return true;
}

bool FTeamPanelTextTest::RunTest(const FString&)
{
	const auto Rate = [](double PerSecond) {
		TStringBuilder<32> Text;
		AppendRate(Text, PerSecond);
		return FString(Text.ToView());
	};
	TestEqual(TEXT("A whole rate has no decimal"), Rate(2.), FString(TEXT("+2/s")));
	TestEqual(TEXT("a fractional rate has one"), Rate(1.5), FString(TEXT("+1.5/s")));
	TestEqual(TEXT("a third rounds to a tenth"), Rate(2. / 3.), FString(TEXT("+0.7/s")));
	TestEqual(TEXT("zero is +0/s"), Rate(0.), FString(TEXT("+0/s")));
	TStringBuilder<96> Pool;
	AppendPool(Pool, 6., 2., 3);
	TestEqual(TEXT("The pool line"), FString(Pool.ToView()), FString(TEXT("pool +6/s Power \u00B7 +2/s Data \u00B7 split 3 ways")));
	Pool.Reset();
	AppendPool(Pool, 4., 1., 1);
	TestTrue(TEXT("A pool of one is not 'ways'"), FString(Pool.ToView()).EndsWith(TEXT("split 1 way")));
	TStringBuilder<32> Chip;
	AppendFortifyChip(Chip, 100.f, 100.f);
	TestEqual(TEXT("A ready Fortify"), FString(Chip.ToView()), FString(TEXT("FORTIFY ready")));
	Chip.Reset();
	AppendFortifyChip(Chip, 147.f, 100.f);
	TestEqual(TEXT("A Fortify on cooldown shows m:ss"), FString(Chip.ToView()), FString(TEXT("FORTIFY 0:47")));
	Chip.Reset();
	AppendFortifyChip(Chip, 100.5f, 100.f);
	TestEqual(TEXT("and rounds a part second up"), FString(Chip.ToView()), FString(TEXT("FORTIFY 0:01")));
	FFlow Flow;
	TStringBuilder<64> Label;
	AppendSendLabel(Label, Flow);
	TestEqual(TEXT("Send names no one before a teammate is chosen"), FString(Label.ToView()), FString(TEXT("SEND")));
	Label.Reset();
	Flow.Teammate = 1;
	AppendSendLabel(Label, Flow);
	TestEqual(TEXT("then the amount, resource and teammate"), FString(Label.ToView()), FString(TEXT("SEND 100 Power to C2")));
	Label.Reset();
	AppendLogText(Label, { 1, 0, EResource::Data, 50, 0.f });
	TestEqual(TEXT("A log line reads sender to recipient"), FString(Label.ToView()), FString(TEXT("C2 \u2192 C1   50 Data")));
	return true;
}

bool FTeamPanelLogTest::RunTest(const FString&)
{
	TestEqual(TEXT("The newest entry is the first row"), LogEntryIndex(5, 0, 0), 4);
	TestEqual(TEXT("then older ones"), LogEntryIndex(5, 0, 2), 2);
	TestEqual(TEXT("Scrolling hides the newest"), LogEntryIndex(5, 2, 0), 2);
	TestEqual(TEXT("A short log leaves empty rows"), LogEntryIndex(2, 0, 2), INDEX_NONE);
	TestEqual(TEXT("An empty log has no rows"), LogEntryIndex(0, 0, 0), INDEX_NONE);
	FFlow Flow;
	ScrollLog(Flow, -1, 5);
	TestEqual(TEXT("The up arrow stops at the newest"), Flow.LogScroll, 0);
	ScrollLog(Flow, 1, 5);
	ScrollLog(Flow, 1, 5);
	ScrollLog(Flow, 1, 5);
	TestEqual(TEXT("The down arrow stops when the oldest is the last row"), Flow.LogScroll, 2);
	ScrollLog(Flow, 1, 2);
	TestEqual(TEXT("A log that fits one page does not scroll"), Flow.LogScroll, 0);
	Flow.LogScroll = 2;
	ClampLog(Flow, 3);
	TestEqual(TEXT("A log that shrank pulls the window back"), Flow.LogScroll, 0);
	// Twenty entries (the replicated limit) scroll through seventeen positions.
	for (int32 Step = 0; Step < 30; ++Step)
		ScrollLog(Flow, 1, 20);
	TestEqual(TEXT("The full log scrolls to its oldest three"), Flow.LogScroll, 17);
	const TArray<FLogEntry> Log = { { 0, 1, EResource::Power, 100, 10.f }, { 1, 2, EResource::Data, 25, 20.f } };
	TestTrue(TEXT("A gift to me after my last look is unseen"), HasUnseenGift(Log, 1, 5.f));
	TestFalse(TEXT("one I sent is not"), HasUnseenGift(Log, 0, 5.f));
	TestFalse(TEXT("a gift between two others is not"), HasUnseenGift(Log, 3, 5.f));
	TestFalse(TEXT("a gift from before my last look is seen"), HasUnseenGift(Log, 1, 10.f));
	TestEqual(TEXT("The newest time is the seen mark"), LatestTime(Log, -1.f), 20.f);
	TestEqual(TEXT("an empty log keeps the mark"), LatestTime(TConstArrayView<FLogEntry>(), 7.f), 7.f);
	return true;
}

bool FTeamPanelEscapeTest::RunTest(const FString&)
{
	using namespace ControllerInputPolicy;
	TestEqual(TEXT("An armed mode closes first, even with the panel open"), EscapeStep(true, true), EEscapeStep::CancelPointerMode);
	TestEqual(TEXT("and with the panel closed"), EscapeStep(true, false), EEscapeStep::CancelPointerMode);
	TestEqual(TEXT("the panel closes before the menu"), EscapeStep(false, true), EEscapeStep::CloseTeamPanel);
	TestEqual(TEXT("the menu is last"), EscapeStep(false, false), EEscapeStep::Screen);
	return true;
}
#endif
