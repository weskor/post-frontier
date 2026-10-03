#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING
#include "ObjectiveAnnouncerFixture.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FObjectiveEventsTest, "CoopRTS.Objectives.Events",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

bool FObjectiveEventsScenario::Update()
{
	UWorld* World = ArmyTestSetup::World();
	const double GameNow = ArmyTestSetup::GameSeconds(World);
	if (World && !bIsolated)
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<ACommandBuilding> It(World); It; ++It)
			It->bProductionEnabled = false;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		for (TActorIterator<ACapturePoint> It(World); It; ++It)
			It->SetActorTickEnabled(false);
		bIsolated = true;
	}
	if (FPlatformTime::Seconds() - Started > 35.)
	{
		Test->AddError(FString::Printf(TEXT("Objective scenario timed out at stage %d"), Stage));
		return true;
	}
	if (Stage == 0)
		return Stage0(World, GameNow);
	if (GameNow - StageStarted < .15)
		return false; // Real Enhanced Input must process the pressed key in a world frame.
	if (Stage == 1)
		return Stage1(GameNow);
	if (Stage == 2)
		return Stage2(GameNow);
	if (Stage == 3)
		return Stage3(GameNow);
	if (Stage == 4)
		return Stage4();
	return false;
}

bool FObjectiveEventsScenario::Stage0(UWorld* World, double GameNow)
{
	if (!World || GameNow < 3. || !ArmyTestSetup::NavigationReady(World))
		return false;
	State = World->GetGameState<ACommandGameState>();
	Controller = ArmyTestSetup::Controller(World);
	Camera = Controller.IsValid() ? Cast<ACommandCamera>(Controller->GetPawn()) : nullptr;
	if (!ArmyTestSetup::MapReady(State.Get()) || !Camera.IsValid() || !Controller->GetPlayerState<ACommandPlayerState>()
		|| Controller->GetPlayerState<ACommandPlayerState>()->CommanderIndex < 0 || State->CaptureSites.IsEmpty())
		return false;
	Announcer = UObjectiveAnnouncer::Get(State.Get());
	if (!Check(Announcer.IsValid(), TEXT("Map exposes the replicated objective history")))
		return true;
	const FVector BeforeEvents = Camera->GetActorLocation();
	if (!Produce(World) || !Check(Camera->GetActorLocation().Equals(BeforeEvents), TEXT("Receiving all objective events never moves the camera")))
		return true;
	Selected = Building(World, 0, ArmyTestSetup::BarracksIndex,
		ArmyTestSetup::FromFriendlyHQ(State.Get(), -600.f, -600.f, 5.f));
	if (!Check(Selected.IsValid(), TEXT("Owned selected-building fixture exists")))
		return true;
	Camera->FocusOn(State->EnemyHeadquarters->GetActorLocation());
	const FVector BeforeSelection = Camera->GetActorLocation();
	Controller->SelectActor(Selected.Get());
	if (!Check(Controller->GetSelectedBuilding() == Selected.Get() && Camera->GetActorLocation().Equals(BeforeSelection),
			TEXT("Selecting a building changes selection without camera movement")))
		return true;
	Press(EKeys::F, true);
	SetStage(1, GameNow);
	return false;
}

bool FObjectiveEventsScenario::Stage1(double GameNow)
{
	Press(EKeys::F, false);
	if (!At(Selected->GetActorLocation(), TEXT("Real F input focuses the selected building")))
		return true;
	Press(EKeys::SpaceBar, true);
	SetStage(2, GameNow);
	return false;
}

bool FObjectiveEventsScenario::Stage2(double GameNow)
{
	Press(EKeys::SpaceBar, false);
	const FObjectiveEvent& Latest = Announcer->GetEvents().Last();
	if (!Check(Controller->GetFocusedAlertSequence() == Latest.Sequence, TEXT("Real Space input focuses newest objective"))
		|| !At(Latest.Location, TEXT("Space camera reaches latest objective location")))
		return true;
	SetStage(3, GameNow); // Release receives its own frame before the next press.
	return false;
}

bool FObjectiveEventsScenario::Stage3(double GameNow)
{
	Press(EKeys::SpaceBar, true);
	SetStage(4, GameNow);
	return false;
}

bool FObjectiveEventsScenario::Stage4()
{
	Press(EKeys::SpaceBar, false);
	const auto Events = Announcer->GetEvents();
	const FObjectiveEvent& Older = Events[Events.Num() - 2];
	if (!Check(Controller->GetFocusedAlertSequence() == Older.Sequence, TEXT("Second real Space input steps back one objective"))
		|| !At(Older.Location, TEXT("Older objective has its own camera location")))
		return true;
	if (!Check(Controller->FocusAlertSequence(Events[0].Sequence), TEXT("Exact-sequence navigation accepts retained history")))
		return true;
	Controller->FocusAlert();
	Controller->FocusAlert();
	if (!Check(Controller->GetFocusedAlertSequence() == Events[0].Sequence, TEXT("Navigation clamps at oldest retained objective"))
		|| !At(Events[0].Location, TEXT("Clamped navigation stays at oldest location")))
		return true;
	const FVector BeforeInvalid = Camera->GetActorLocation();
	if (!Check(!Controller->FocusAlertSequence(-1) && Camera->GetActorLocation().Equals(BeforeInvalid),
			TEXT("Missing sequence cannot move the camera")))
		return true;
	const int32 OlderSequence = Older.Sequence;
	if (!Check(Controller->FocusAlertSequence(OlderSequence), TEXT("Exact-sequence navigation accepts an older retained alert"))
		|| !At(Older.Location, TEXT("Exact-sequence camera navigation reaches the older alert")))
		return true;
	Controller->FocusAlert();
	if (!Check(Controller->GetFocusedAlertSequence() == Events[Events.Num() - 3].Sequence, TEXT("Navigation continues older from an exact-sequence cursor")))
		return true;
	return FinishNavigation(OlderSequence);
}

bool FObjectiveEventsScenario::FinishNavigation(int32 OlderSequence)
{
	const FVector BeforeNew = Camera->GetActorLocation();
	Announcer->Raise(TEXT("region_captured"), 0, State->FriendlyHeadquarters->GetActorLocation(), {});
	if (!Check(Camera->GetActorLocation().Equals(BeforeNew), TEXT("New event does not auto-focus from an older cursor")))
		return true;
	Controller->FocusAlert();
	if (!Check(Controller->GetFocusedAlertSequence() == Announcer->GetEvents().Last().Sequence, TEXT("New objective resets navigation to newest"))
		|| !At(Announcer->GetEvents().Last().Location, TEXT("Reset reaches new objective")))
		return true;
	const int32 FirstRetained = Announcer->GetEvents()[0].Sequence;
	const int32 FillCount = UObjectiveAnnouncer::HistoryLimit - Announcer->GetEvents().Num();
	for (int32 Index = 0; Index < FillCount; ++Index)
		Announcer->Raise(TEXT("region_captured"), 0, State->FriendlyHeadquarters->GetActorLocation(), {});
	if (!Check(Announcer->GetEvents().Num() == UObjectiveAnnouncer::HistoryLimit
				&& Announcer->GetEvents()[0].Sequence == FirstRetained,
			TEXT("Filling the ring does not evict its oldest event")))
		return true;
	const int32 Next = Announcer->GetEvents().Last().Sequence + 1;
	const int32 Added = 2 * UObjectiveAnnouncer::HistoryLimit + 3;
	for (int32 Index = 0; Index < Added; ++Index)
		Announcer->Raise(TEXT("region_captured"), 0, State->FriendlyHeadquarters->GetActorLocation(), {});
	const auto Bounded = Announcer->GetEvents();
	if (!Check(Bounded.Num() == UObjectiveAnnouncer::HistoryLimit
				&& Bounded[0].Sequence == Next + Added - UObjectiveAnnouncer::HistoryLimit,
			TEXT("Repeated ring wrap discards only oldest events while retaining bounded history")))
		return true;
	for (int32 Index = 1; Index < Bounded.Num(); ++Index)
		if (!Check(Bounded[Index].Sequence == Bounded[Index - 1].Sequence + 1, TEXT("Retained history sequences remain monotonic and contiguous")))
			return true;
	if (!Check(!Controller->FocusAlertSequence(OlderSequence), TEXT("Evicted sequence is no longer navigable")))
		return true;
	Test->AddInfo(TEXT("All 11 objective producers passed attribution/region/transition checks; real F/Space, selection stability, navigation and bounded history passed. Rendered feed clicks are covered by HUD verification."));
	return true;
}

bool FObjectiveEventsTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FObjectiveEventsScenario(this));
	return true;
}
#endif
