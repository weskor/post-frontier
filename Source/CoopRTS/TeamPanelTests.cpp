#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "CommandHUD.h"
#include "Commands/GiftCommandComponent.h"
#include "GenericPlatform/GenericPlatformInputDeviceMapper.h"
#include "HUD/HUDPanels.h"
#include "HUD/TeamPanel.h"
#include "InputKeyEventArgs.h"
#include "TeamEconomyFixture.h"

// The Team panel on a real controller and HUD: a gift through the owning RPC component, and the panel's flow,
// geometry, keys and Esc order. Isolated from JEV and paid production by the team-economy fixture.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPanelClientGiftTest, "CoopRTS.TeamPanel.ClientGift",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FTeamPanelSurfaceTest, "CoopRTS.TeamPanel.Surface",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace
{
using namespace CommandHUDPanels;
using namespace TeamPanelPolicy;

FString RefusalOf(const ACommandPlayerController& Controller)
{
	FString Text;
	float Opacity;
	return Controller.GetTeamRefusal(Text, Opacity) && Opacity == 1.f ? Text : FString();
}

FString LogLine(const ACommandGameState& State, int32 Row)
{
	FLogBuffer Log;
	UGiftCommandComponent::ReadLog(State, Log);
	const int32 Index = LogEntryIndex(Log.Num(), 0, Row);
	TStringBuilder<64> Text;
	if (Index != INDEX_NONE)
		AppendLogText(Text, Log[Index]);
	return FString(Text.ToView());
}

FString Commander(int32 Slot)
{
	TStringBuilder<16> Name;
	AppendCommander(Name, Slot);
	return FString(Name.ToView());
}
}

bool FTeamPanelClientGiftTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FTeamEconomyScenario(this, 2, [](FTeamEconomyFixture& F) {
		FAutomationTestBase& T = *F.Test;
		ACommandPlayerController* Sender = F.Controller;
		ACommandPlayerState* From = F.Wallets[0];
		ACommandPlayerState* To = F.Wallets[1];
		ACommandPlayerController* Peer = F.World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* Outsider = F.Spawn(4, false);
		if (!T.TestTrue(TEXT("The recipient's controller and an outsider spawn"), Peer && Outsider))
			return;
		Peer->SetPlayerState(To);
		From->Resources = 300;
		From->Data = 80;
		To->Resources = 5;
		Sender->GiftCommands->ServerGift(To, EEconomyResource::Power, 100);
		T.TestTrue(TEXT("A Power gift through the owning component moves Power atomically"),
			From->Resources == 200 && To->Resources == 105 && From->Data == 80 && To->Data == 0);
		Sender->GiftCommands->ServerGift(To, EEconomyResource::Data, 80);
		T.TestTrue(TEXT("and a Data gift moves Data without touching Power"),
			From->Data == 0 && To->Data == 80 && From->Resources == 200 && To->Resources == 105);
		T.TestEqual(TEXT("Both gifts are in the replicated team log"), F.State->GiftLog.Num(), 2);
		const FString Newest = Commander(From->CommanderIndex) + TEXT(" \u2192 ") + Commander(To->CommanderIndex);
		for (const ACommandPlayerController* Viewer : { static_cast<const ACommandPlayerController*>(Sender), static_cast<const ACommandPlayerController*>(Peer) })
		{
			const FContext Context = MakeContext(Viewer);
			T.TestEqual(TEXT("Each commander's panel reads the newest gift"), LogLine(*Context.State, 0), Newest + TEXT("   80 Data"));
			T.TestEqual(TEXT("and the one before it"), LogLine(*Context.State, 1), Newest + TEXT("   100 Power"));
		}
		T.TestTrue(TEXT("Only the recipient has an unseen gift"), Peer->HasUnseenGift() && !Sender->HasUnseenGift());
		// Every refusal changes nothing, logs nothing and carries the panel's wording.
		const auto Refused = [&](const TCHAR* Why, ACommandPlayerState* Recipient, EEconomyResource Resource, int32 Amount,
								 const FString& Text) {
			Sender->GiftCommands->ServerGift(Recipient, Resource, Amount);
			T.TestEqual(FString(Why) + TEXT(" says why"), RefusalOf(*Sender), Text);
			T.TestTrue(FString(Why) + TEXT(" moves and logs nothing"),
				From->Resources == 200 && To->Resources == 105 && From->Data == 0 && To->Data == 80 && F.State->GiftLog.Num() == 2);
		};
		Refused(TEXT("An overdraft"), To, EEconomyResource::Power, 201, TEXT("Not enough Power: you have 200"));
		Refused(TEXT("A Data overdraft"), To, EEconomyResource::Data, 1, TEXT("Not enough Data: you have 0"));
		Refused(TEXT("A zero gift"), To, EEconomyResource::Power, 0, TEXT("Pick an amount above 0"));
		Refused(TEXT("A gift to nobody"), nullptr, EEconomyResource::Power, 10, TEXT("Pick a teammate first"));
		Refused(TEXT("A gift to a commander outside the team"), Outsider, EEconomyResource::Power, 10,
			Commander(Outsider->CommanderIndex) + TEXT(" left the team"));
		F.State->MatchResult = EMatchResult::Victory;
		Refused(TEXT("A gift after the battle"), To, EEconomyResource::Power, 10, TEXT("Gifting is closed: the battle is over"));
		F.State->MatchResult = EMatchResult::Ongoing;
		Peer->SetPlayerState(nullptr);
		Peer->Destroy();
		Outsider->Destroy();
	}));
	return true;
}

namespace
{
using namespace ArmyTestSetup;

// Staged: Esc and Tab act on the frame after the key event, so each key gets its own stage.
class FSurfaceScenario : public IAutomationLatentCommand
{
public:
	explicit FSurfaceScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}
	bool Update() override;

private:
	bool Prepare();
	bool Layouts(bool bOpen);
	bool RunFlow();
	bool Refusals();
	bool Scroll();
	void Key(FKey Value, EInputEvent Event = IE_Pressed)
	{
		PC->InputKey(FInputKeyEventArgs(nullptr, IPlatformInputDeviceMapper::Get().GetDefaultInputDevice(), Value, Event, FPlatformTime::Cycles64()));
	}
	bool Click(EHUDAction Action);
	bool Check(bool bCondition, const TCHAR* Message)
	{
		if (!bCondition)
			Test->AddError(FString::Printf(TEXT("Stage %d: %s"), Stage, Message));
		return bCondition;
	}
	FAutomationTestBase* Test;
	double Started;
	int32 Stage = 0;
	TUniquePtr<FTeamEconomyFixture> Fixture;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	ACommandPlayerController* PC = nullptr;
	ACommandGameState* State = nullptr;
};

bool FSurfaceScenario::Click(EHUDAction Action)
{
	const ACommandHUD* HUD = Cast<ACommandHUD>(PC->GetHUD());
	FVector2D Point;
	return Check(HUD && HUD->FindActionScreenPosition(Action, Point), TEXT("The action has a visible hit target"))
		&& Check(PC->HandleHUDClick(Point), TEXT("A real HUD click dispatches the action"));
}

bool FSurfaceScenario::Prepare()
{
	Fixture = FTeamEconomyFixture::Create(World(), Test);
	if (!Fixture)
		return false;
	if (!Fixture->Reset(4))
		return Check(false, TEXT("Fixture needs regions 2, 3 and 4"));
	PC = Fixture->Controller;
	State = Fixture->State;
	Fixture->Wallets[0]->Resources = 340;
	Fixture->Wallets[0]->Data = 80;
	Fixture->Wallets[1]->FortifyReadyAt = State->GetServerWorldTimeSeconds() + 47.f;
	return true;
}

// Geometry at both required resolutions, for three teammates (the tallest panel).
bool FSurfaceScenario::Layouts(bool bOpen)
{
	const FContext Context = MakeContext(PC);
	bool bOk = true;
	for (const FVector2D& Size : { FVector2D(1600.f, 900.f), FVector2D(1280.f, 720.f) })
	{
		const FLayout Layout = MakeLayout(Context, Size.X, Size.Y);
		bOk &= Check(Layout.TeamButton.W == 96.f && Layout.TeamButton.H == 32.f && Layout.TeamButton.Y == Layout.Pause.Y
				&& Layout.TeamButton.Right() + Gap == Layout.Pause.X,
			TEXT("The TEAM opener is 96x32, left of Pause"));
		bOk &= Check(bOpen == (Layout.TeamPanel.W > 0.f), TEXT("The panel slot exists exactly while open"));
		if (!bOpen)
			continue;
		bOk &= Check(Layout.TeamPanel.W == 390.f && Layout.Alerts.H == 0.f && Layout.TeamPanel.X == Layout.Alerts.X,
			TEXT("The 390 px panel takes the alert column and the feed gives up its height"));
		bOk &= Check(Layout.TeamPanel.Bottom() <= Layout.Build.Y && !Layout.TeamPanel.Intersects(Layout.Build)
				&& !Layout.TeamPanel.Intersects(Layout.Pause) && !Layout.TeamPanel.Intersects(Layout.Objectives)
				&& !Layout.TeamPanel.Intersects(Layout.ForceBar),
			TEXT("Three teammates fit the column without covering the build bar, Pause or the strip"));
		ForEachButton(Context, Layout, [&](const FButton& Button) {
			if (!IsTeamAction(Button.Action))
				return;
			bOk &= Check(Button.Rect.W >= 28.f && Button.Rect.H >= 28.f - (Button.Action == EHUDAction::TeamClose ? 2.f : 0.f),
				TEXT("Every Team target is at least 28 px"));
			bOk &= Check(HitTest(Context, Layout, Button.Rect.Center()) == Button.Action, TEXT("Each Team button wins its own centre"));
		});
	}
	return bOk;
}

bool FSurfaceScenario::RunFlow()
{
	FTeamEconomyFixture& F = *Fixture;
	const int32 Slot = F.Wallets[1]->CommanderIndex;
	const FFlow& Flow = PC->GetTeamFlow();
	bool bOk = Click(EHUDAction::TeamRow0) && Check(Flow.Teammate == Slot, TEXT("A row click chooses that teammate"));
	bOk = bOk && Click(EHUDAction::TeamData) && Check(Flow.Resource == EResource::Data && Flow.Amount == 25, TEXT("DATA restarts at 25"));
	bOk = bOk && Click(EHUDAction::TeamPreset2) && Check(Flow.Amount == 50, TEXT("The third Data preset is 50"));
	bOk = bOk && Click(EHUDAction::TeamStepUp) && Check(Flow.Amount == 55, TEXT("+ adds 5 Data"));
	bOk = bOk && Click(EHUDAction::TeamStepDown) && Click(EHUDAction::TeamPresetAll)
		&& Check(Flow.Amount == 80, TEXT("ALL is the whole Data balance"));
	bOk = bOk && Click(EHUDAction::TeamSend)
		&& Check(F.Wallets[0]->Data == 0 && F.Wallets[1]->Data == 80 && State->GiftLog.Num() == 1,
			TEXT("Send moves the Data and logs the gift"));
	bOk = bOk && Click(EHUDAction::TeamPower) && Check(Flow.Resource == EResource::Power && Flow.Amount == 100, TEXT("POWER restarts at 100"));
	bOk = bOk && Click(EHUDAction::TeamPreset2) && Click(EHUDAction::TeamStepUp)
		&& Check(Flow.Amount == 210, TEXT("Power steps by 10 past a preset"));
	bOk = bOk && Click(EHUDAction::TeamSend)
		&& Check(F.Wallets[0]->Resources == 130 && F.Wallets[1]->Resources == 210 && State->GiftLog.Num() == 2,
			TEXT("A Power Send moves exactly the shown amount"));
	return bOk;
}

bool FSurfaceScenario::Refusals()
{
	bool bOk = Click(EHUDAction::TeamPreset2) && Click(EHUDAction::TeamSend)
		&& Check(RefusalOf(*PC) == TEXT("Not enough Power: you have 130"), TEXT("A disabled Send still explains itself on click"))
		&& Check(State->GiftLog.Num() == 2, TEXT("and sends nothing"));
	bOk = bOk && Click(EHUDAction::TeamStepUp) && Check(PC->GetTeamFlow().Amount == 130, TEXT("The stepper stops at the balance"));
	const FContext Context = MakeContext(PC);
	FButton Send{};
	ForEachButton(Context, MakeLayout(Context, 1600.f, 900.f), [&](const FButton& Button) {
		if (Button.Action == EHUDAction::TeamSend)
			Send = Button;
	});
	return bOk && Check(Send.Available(), TEXT("Send is enabled once the amount is affordable"));
}

bool FSurfaceScenario::Scroll()
{
	for (int32 Amount = 1; Amount <= 3; ++Amount)
		FCommandService::Gift(Fixture->Wallets[0], Fixture->Wallets[1], EEconomyResource::Power, Amount);
	// Five gifts in the log: two rows are hidden above the three shown.
	bool bOk = Click(EHUDAction::TeamLogDown) && Check(PC->GetTeamFlow().LogScroll == 1, TEXT("The down arrow scrolls to older gifts"));
	bOk = bOk && Click(EHUDAction::TeamLogDown) && Click(EHUDAction::TeamLogDown)
		&& Check(PC->GetTeamFlow().LogScroll == 2, TEXT("Scrolling stops at the oldest gift"));
	bOk = bOk && Click(EHUDAction::TeamLogUp) && Click(EHUDAction::TeamLogUp) && Click(EHUDAction::TeamLogUp)
		&& Check(PC->GetTeamFlow().LogScroll == 0, TEXT("and at the newest"));
	return bOk;
}

bool FSurfaceScenario::Update()
{
	if (FPlatformTime::Seconds() - Started > 60.)
	{
		Test->AddError(FString::Printf(TEXT("Team panel scenario exceeded 60 seconds at stage %d"), Stage));
		return true;
	}
	if (Stage == 0)
	{
		if (!Prepare())
			return false;
		Check(!PC->IsTeamPanelOpen() && Layouts(false), TEXT("The panel starts closed with the opener in place"));
		Key(EKeys::Tab);
		Stage = 1;
		return false;
	}
	if (Stage == 1)
	{
		Key(EKeys::Tab, IE_Released);
		if (Check(PC->IsTeamPanelOpen() && Layouts(true), TEXT("Tab opens the panel at both resolutions")) && RunFlow() && Refusals() && Scroll())
		{
			PC->ToggleFortifyTargeting();
			Check(PC->IsFortifyTargeting(), TEXT("Fortify targeting arms with the panel open"));
			Key(EKeys::Escape);
		}
		Stage = 2;
		return false;
	}
	// Each Esc is released one frame and pressed the next, so Enhanced Input sees a fresh press.
	if (Stage == 2 || Stage == 4 || Stage == 6)
	{
		if (Stage == 2)
			Check(!PC->IsFortifyTargeting() && PC->IsTeamPanelOpen(), TEXT("The first Esc ends the armed mode and leaves the panel"));
		else if (Stage == 4)
			Check(!PC->IsTeamPanelOpen() && PC->GetUIScreen() == ECommandScreen::Game, TEXT("The second Esc closes the panel, not the game"));
		Key(EKeys::Escape, IE_Released);
		++Stage;
		return false;
	}
	if (Stage == 3 || Stage == 5)
	{
		Key(EKeys::Escape);
		++Stage;
		return false;
	}
	Check(PC->GetUIScreen() == ECommandScreen::Pause, TEXT("The third Esc opens the menu"));
	Click(EHUDAction::Resume);
	Check(PC->GetUIScreen() == ECommandScreen::Game, TEXT("Resume leaves the menu"));
	Fixture->Teardown();
	return true;
}
}

bool FTeamPanelSurfaceTest::RunTest(const FString&)
{
	ADD_LATENT_AUTOMATION_COMMAND(FSurfaceScenario(this));
	return true;
}
#endif
