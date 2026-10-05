#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyGroup.h"
#include "ArmyTestSetup.h"
#include "CommandCamera.h"
#include "EngineUtils.h"
#include "FailoverNode.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformTime.h"
#include "Headquarters.h"
#include "MapRegion.h"
#include "Misc/Paths.h"
#include "ObjectiveAnnouncer.h"
#include "Rules/AnnouncerPolicy.h"
#include "Rules/GuardedHqView.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedHqLeftoversViewTest, "CoopRTS.Visual.HqLeftovers.Surface",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Rendered proof for the guarded-HQ readability pass (scope guarded-hq-leftovers-view, run on request): the objective feed's
// node-loss and emergency rows and the minimap's Failover Node marks, standing and lost, beside HQs that also carry a Fortify
// ring and an offline X, each at 1600x900 and 1280x720. A person inspects the PNGs; the scenario asserts the events behind them.
namespace GuardedHqLeftoversViewTests
{
struct FWorldRefs
{
	UWorld* World;
	ACommandGameState* State;
	ACommandPlayerController* PC;
};

struct FState
{
	const TCHAR* Name;
	TFunction<void(const FWorldRefs&)> Arrange;
	// The event the feed must hold after Arrange: the newest objective event's id kind, and its number (-1: unchecked).
	GuardedHqView::EFeedKind ExpectedKind;
	int32 ExpectedNumber;
};

struct FResolution
{
	int32 Width;
	int32 Height;
};
constexpr FResolution Resolutions[] = { { 1600, 900 }, { 1280, 720 } };

// The forces that deliver the blows stand outside both mains; they are dismissed once the state is staged, so the
// captures show the HUD's own marks and not the strikers beside them.
TArray<TWeakObjectPtr<AArmyGroup>> Strikers;

AArmyUnit* Striker(const FWorldRefs& W, int32 VictimTeam)
{
	static int32 Serial = 120;
	const AHeadquarters& Other = VictimTeam == 0 ? *W.State->EnemyHeadquarters : *W.State->FriendlyHeadquarters;
	AArmyGroup* Group = ArmyTestSetup::SpawnGroup(W.World, VictimTeam == 0 ? nullptr : W.PC, VictimTeam == 0 ? -1 : Serial++,
		Other.GetActorLocation() + FVector(-2500., 500., 100.));
	if (!Group || Group->GetUnits().IsEmpty())
		return nullptr;
	Group->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Group->GetUnits())
		Unit->SetActorTickEnabled(false);
	Strikers.Add(Group);
	return Group->GetUnits()[0];
}

void DismissStrikers()
{
	for (const TWeakObjectPtr<AArmyGroup>& Group : Strikers)
		if (Group.IsValid())
			Group->Destroy();
	Strikers.Reset();
}

void ForEachHq(const FWorldRefs& W, TFunctionRef<void(AHeadquarters&)> Visit)
{
	Visit(*W.State->FriendlyHeadquarters);
	Visit(*W.State->EnemyHeadquarters);
}

void KillNode(AHeadquarters& Home, AArmyUnit* Striker)
{
	for (const TWeakObjectPtr<AFailoverNode>& Node : Home.GetNodes())
		if (Striker && Node.IsValid() && Node->IsAlive())
		{
			Node->ReceiveAttack(100000, Striker);
			return;
		}
}

TArray<FState> States()
{
	using GuardedHqView::EFeedKind;
	return {
		{ TEXT("1-nodes-standing"), [](const FWorldRefs&) {}, EFeedKind::Plain, -1 },
		{ TEXT("2-node-lost"), [](const FWorldRefs& W) {
			 ForEachHq(W, [&W](AHeadquarters& Home) { KillNode(Home, Striker(W, Home.TeamIndex)); });
		 },
			EFeedKind::NodeLost, 1 },
		// A cast Fortify puts its ring on each main, where the node marks must stay clear of it.
		{ TEXT("3-fortify-ring"), [](const FWorldRefs& W) {
			 ForEachHq(W, [&W](AHeadquarters& Home) {
				 const AMapRegion* Main = W.State->FindRegionAt(Home.GetActorLocation());
				 for (AMapRegion* Region : W.State->Regions)
					 if (Main && Region == Main)
						 Region->StartFortify(Home.TeamIndex, 0);
			 });
		 },
			EFeedKind::NodeLost, 1 },
		{ TEXT("4-offline-emergency"), [](const FWorldRefs& W) {
			 ForEachHq(W, [&W](AHeadquarters& Home) {
				 AArmyUnit* Hit = Striker(W, Home.TeamIndex);
				 KillNode(Home, Hit);
				 if (Hit)
					 Home.ReceiveAttack(100000, Hit);
			 });
		 },
			EFeedKind::Emergency, -1 },
	};
}

class FGuardedHqLeftoversCapture : public IAutomationLatentCommand
{
public:
	explicit FGuardedHqLeftoversCapture(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()), Shots(States()) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 300.)
		{
			Test->AddError(FString::Printf(TEXT("Guarded HQ leftover captures timed out at state %d"), Index));
			return true;
		}
		UWorld* World = ArmyTestSetup::World();
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandCamera* Camera = PC ? Cast<ACommandCamera>(PC->GetPawn()) : nullptr;
		if (!ArmyTestSetup::MapReady(State) || !Camera || ArmyTestSetup::GameSeconds(World) < 3.)
			return false;
		if (State->FriendlyHeadquarters->GetNodes().Num() != 2 || State->EnemyHeadquarters->GetNodes().Num() != 2)
		{
			Test->AddError(TEXT("The map must place two Failover Nodes per HQ (regenerate AvailabilityZoneV2)"));
			return true;
		}
		if (Index >= Shots.Num())
		{
			Test->AddInfo(FString::Printf(TEXT("Guarded HQ leftover captures written to %s"), *Directory()));
			return true;
		}
		const FWorldRefs Refs{ World, State, PC };
		const FState& Shot = Shots[Index];
		const FResolution& Size = Resolutions[Resolution];
		const FString Path = FPaths::Combine(Directory(), FString::Printf(TEXT("%s-%dx%d.png"), Shot.Name, Size.Width, Size.Height));
		if (Frames == 0 && !Begin(Refs, Shot, Size, Path, *Camera))
			return true;
		// Let the resolution and the feed settle (1 s), then capture while the feed rows (8 s) still stand.
		if (++Frames == 60)
			FScreenshotRequest::RequestScreenshot(Path, false, false);
		if (Frames > 60 && IFileManager::Get().FileSize(*Path) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Captured %s (%lld bytes)"), *Path, IFileManager::Get().FileSize(*Path)));
			Frames = 0;
			if (++Resolution == UE_ARRAY_COUNT(Resolutions))
			{
				Resolution = 0;
				++Index;
			}
			return false;
		}
		if (Frames > 900)
		{
			Test->AddError(FString::Printf(TEXT("No screenshot produced for %s"), *Path));
			return true;
		}
		return false;
	}

	// Stages the state at the first resolution, resizes the viewport and frames the camera. False: the staging failed.
	bool Begin(const FWorldRefs& W, const FState& Shot, const FResolution& Size, const FString& Path, ACommandCamera& Camera)
	{
		IFileManager::Get().Delete(*Path);
		// Real play never reaches these states in seconds: the match must not end or JEV act while they are staged.
		for (TActorIterator<AEnemyCommander> It(W.World); It; ++It)
			It->SetActorTickEnabled(false);
		if (Resolution == 0)
		{
			Shot.Arrange(W);
			DismissStrikers();
			if (!CheckEvents(*W.State, Shot))
				return false;
		}
		W.PC->ConsoleCommand(FString::Printf(TEXT("r.SetRes %dx%dw"), Size.Width, Size.Height));
		Camera.FocusOn(W.State->EnemyHeadquarters->GetActorLocation());
		return true;
	}

private:
	// The newest objective event is the row under test; log the title the feed composes for it.
	bool CheckEvents(const ACommandGameState& State, const FState& Shot) const
	{
		const UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(&State);
		if (Shot.ExpectedKind == GuardedHqView::EFeedKind::Plain)
			return true;
		if (!Announcer || Announcer->GetEvents().IsEmpty())
		{
			Test->AddError(FString::Printf(TEXT("%s: no objective event was raised"), Shot.Name));
			return false;
		}
		const FObjectiveEvent& Event = Announcer->GetEvents().Last();
		TStringBuilder<64> Id;
		Event.Id.AppendString(Id);
		const GuardedHqView::EFeedKind Kind = GuardedHqView::Classify(Id.ToView());
		const AnnouncerPolicy::FDefinition* Definition = AnnouncerPolicy::Find(Event.Id);
		TStringBuilder<160> Title;
		GuardedHqView::AppendFeedTitle(Title, Definition ? FStringView(Definition->Text) : FStringView(), Kind,
			Kind == GuardedHqView::EFeedKind::NodeLost ? Event.NodesLeft() : Event.RestoredPercent());
		Test->AddInfo(FString::Printf(TEXT("%s: newest feed row reads \"%s\""), Shot.Name, Title.ToString()));
		const bool bKind = Kind == Shot.ExpectedKind;
		const bool bNumber = Shot.ExpectedNumber < 0 || Event.NodesLeft() == Shot.ExpectedNumber;
		if (!bKind || !bNumber)
			Test->AddError(FString::Printf(TEXT("%s: newest event %s has the wrong kind or number (%d)"), Shot.Name, *Event.Id.ToString(), Event.NodesLeft()));
		return bKind && bNumber;
	}

	static FString Directory() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("GuardedHqLeftoversCaptures")); }

	FAutomationTestBase* Test;
	int32 Index = 0;
	int32 Resolution = 0;
	int32 Frames = 0;
	double Started;
	TArray<FState> Shots;
};
}

bool FGuardedHqLeftoversViewTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(GuardedHqLeftoversViewTests::FGuardedHqLeftoversCapture(this));
	return true;
}

#endif
