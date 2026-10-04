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
#include "Misc/Paths.h"
#include "UnrealClient.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGuardedHqViewTest, "CoopRTS.Visual.GuardedHq.Surface",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Rendered proof for surface 9 (scope guarded-hq-view, run on request): the real game on Habitable Zone v2
// renders the objective strip and the HQ labels in each guarded-HQ state, with the HUD on. Each PNG must
// exist and be non-empty; a person inspects them.
namespace GuardedHqViewTests
{
struct FWorldRefs
{
	UWorld* World;
	ACommandGameState* State;
	ACommandPlayerController* PC;
};

AArmyUnit* Standing(const FWorldRefs& W, int32 Team, const AHeadquarters& Main, float Offset)
{
	static int32 Serial = 80;
	const FVector At = Main.GetActorLocation() + FVector(Offset, 500.f, 100.f);
	AArmyGroup* Group = ArmyTestSetup::SpawnGroup(W.World, Team == 0 ? W.PC : nullptr, Team == 0 ? Serial++ : -1, At);
	if (!Group || Group->GetUnits().IsEmpty())
		return nullptr;
	Group->SetActorTickEnabled(false);
	for (AArmyUnit* Unit : Group->GetUnits())
		Unit->SetActorTickEnabled(false);
	return Group->GetUnits()[0];
}

void ClearUnits(const FWorldRefs& W)
{
	for (TActorIterator<AArmyGroup> It(W.World); It; ++It)
		It->Destroy();
}

// Both HQs in the same state at once, so one capture shows the strip's two bars.
void ForEachHq(const FWorldRefs& W, TFunctionRef<void(AHeadquarters&)> Visit)
{
	Visit(*W.State->FriendlyHeadquarters);
	Visit(*W.State->EnemyHeadquarters);
}

void KillNode(AHeadquarters& Home, AArmyUnit& Striker)
{
	for (const TWeakObjectPtr<AFailoverNode>& Node : Home.GetNodes())
		if (Node.IsValid() && Node->IsAlive())
		{
			Node->ReceiveAttack(100000, &Striker);
			return;
		}
}

struct FState
{
	const TCHAR* Name;
	TFunction<void(const FWorldRefs&)> Arrange;
};

// The strikers stand outside both mains; presence units stand inside them.
AArmyUnit* Striker(const FWorldRefs& W, int32 VictimTeam)
{
	const AHeadquarters& Other = VictimTeam == 0 ? *W.State->EnemyHeadquarters : *W.State->FriendlyHeadquarters;
	return Standing(W, VictimTeam == 0 ? 5 : 0, Other, -2500.f);
}

TArray<FState> States()
{
	return {
		{ TEXT("1-nodes-standing"), [](const FWorldRefs&) {} },
		{ TEXT("2-one-node-lost"), [](const FWorldRefs& W) {
			 ForEachHq(W, [&W](AHeadquarters& Home) { KillNode(Home, *Striker(W, Home.TeamIndex)); });
		 } },
		{ TEXT("3-offline-emergency"), [](const FWorldRefs& W) {
			 ForEachHq(W, [&W](AHeadquarters& Home) {
				 AArmyUnit* Hit = Striker(W, Home.TeamIndex);
				 KillNode(Home, *Hit);
				 Home.ReceiveAttack(100000, Hit);
			 });
		 } },
		{ TEXT("4-offline-holding"), [](const FWorldRefs& W) {
			 ClearUnits(W);
			 ForEachHq(W, [&W](AHeadquarters& Home) {
				 Standing(W, Home.TeamIndex == 0 ? 5 : 0, Home, 0.f);
				 Home.Tick(38.f);
			 });
		 } },
		{ TEXT("5-offline-paused"), [](const FWorldRefs& W) {
			 ForEachHq(W, [&W](AHeadquarters& Home) { Standing(W, Home.TeamIndex, Home, 300.f); });
		 } },
		{ TEXT("6-offline-decaying"), [](const FWorldRefs& W) { ClearUnits(W); } },
		{ TEXT("7-back-online"), [](const FWorldRefs& W) { ForEachHq(W, [](AHeadquarters& Home) { Home.Tick(100.f); }); } },
	};
}

class FGuardedHqViewCapture : public IAutomationLatentCommand
{
public:
	explicit FGuardedHqViewCapture(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()), Shots(States()) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 240.)
		{
			Test->AddError(FString::Printf(TEXT("Guarded HQ captures timed out at state %d"), Index));
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
			Test->AddInfo(FString::Printf(TEXT("Guarded HQ captures written to %s"), *Directory()));
			return true;
		}
		const FWorldRefs Refs{ World, State, PC };
		const FString Path = FPaths::Combine(Directory(), FString(Shots[Index].Name) + TEXT(".png"));
		if (!bRequested)
		{
			IFileManager::Get().Delete(*Path);
			// Real play never reaches these states in seconds: the match must not end or JEV act while they are staged.
			for (TActorIterator<AEnemyCommander> It(World); It; ++It)
				It->SetActorTickEnabled(false);
			Shots[Index].Arrange(Refs);
			Camera->FocusOn(State->EnemyHeadquarters->GetActorLocation());
			for (int32 Step = 0; Step < 4; ++Step)
				Camera->Zoom(-1.f);
			Frames = 0;
			bRequested = true;
			return false;
		}
		if (++Frames == 150)
			FScreenshotRequest::RequestScreenshot(Path, false, false);
		if (Frames > 150 && IFileManager::Get().FileSize(*Path) > 0)
		{
			Test->AddInfo(FString::Printf(TEXT("Captured %s (%lld bytes)"), *Path, IFileManager::Get().FileSize(*Path)));
			++Index;
			bRequested = false;
			return false;
		}
		if (Frames > 900)
		{
			Test->AddError(FString::Printf(TEXT("No screenshot produced for %s"), Shots[Index].Name));
			return true;
		}
		return false;
	}

private:
	static FString Directory() { return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("GuardedHqCaptures")); }

	FAutomationTestBase* Test;
	int32 Index = 0;
	int32 Frames = 0;
	bool bRequested = false;
	double Started;
	TArray<FState> Shots;
};
}

bool FGuardedHqViewTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(GuardedHqViewTests::FGuardedHqViewCapture(this));
	return true;
}

#endif
