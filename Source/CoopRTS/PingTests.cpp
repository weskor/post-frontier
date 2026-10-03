#if WITH_DEV_AUTOMATION_TESTS && !UE_BUILD_SHIPPING

#include "Misc/AutomationTest.h"
#include "ArmyTestSetup.h"
#include "ArmyUnit.h"
#include "Commands/PingCommandComponent.h"
#include "HAL/PlatformTime.h"
#include "ObjectiveAnnouncer.h"
#include "HUD/HUDPanels.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FPingPlacementExpiryTest, "CoopRTS.Pings.PlacementExpiry",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

namespace PingScenarioTests
{
// Run alone in a fresh standalone world. Groups are explicit, unpaid communication
// fixtures; production, income and JEV are isolated, not accelerated under test.
class FPlacementExpiryScenario : public IAutomationLatentCommand
{
public:
	explicit FPlacementExpiryScenario(FAutomationTestBase* InTest)
		: Test(InTest), Started(FPlatformTime::Seconds()) {}

	bool Update() override
	{
		const double RealNow = FPlatformTime::Seconds();
		if (RealNow - Started > 45.)
			return Fail(TEXT("Ping placement/expiry scenario exceeded 45 seconds"));
		UWorld* World = ArmyTestSetup::World();
		ACommandPlayerController* PC = World ? ArmyTestSetup::Controller(World) : nullptr;
		ACommandGameState* State = World ? World->GetGameState<ACommandGameState>() : nullptr;
		ACommandPlayerState* Sender = PC ? PC->GetPlayerState<ACommandPlayerState>() : nullptr;
		if (!PC || !Sender || Sender->CommanderIndex < 0 || !ArmyTestSetup::MapReady(State)
			|| !IsValid(State->EnemyCommander) || World->GetTimeSeconds() < 3.f)
			return false;
		if (Stage == 0)
		{
			if (!Setup(World, PC, State))
				return true;
			Ground = ArmyTestSetup::FromFriendlyHQ(State, 550.f, -500.f, 0.f);
			if (!Check(State->Arena->ContainsTravel(Ground), TEXT("Map-derived ground ping is inside the arena")))
				return true;
			if (!Check(FCommandService::Ping(PC, Ground).IsAccepted(), TEXT("Ground command is accepted"))
				|| !Delivery(PC, Sender, TEXT("ping_look_here"), Ground, 0, 1))
				return true;
			const FCommandResult Throttled = FCommandService::Ping(PC, Ground + FVector(75.f, 0.f, 0.f));
			if (!Check(!Throttled.IsAccepted() && !Throttled.Message.IsEmpty(),
					TEXT("Immediate second ping is authoritatively throttled with an explanation"))
				|| !Counts(PC, 1))
				return true;
			FirstTime = PC->PingCommands->GetEvents().Last().ServerTime;
			LastAccepted = RealNow;
			Stage = 1;
			return false;
		}
		if (!Ally.IsValid() || !Opponent.IsValid() || !TeammateForce.IsValid() || !EnemyForce.IsValid() || !OwnForce.IsValid())
			return Fail(TEXT("Isolated ping participants must survive"));
		if (Stage <= 3 && RealNow - LastAccepted < 2.05)
			return false;
		if (Stage == 1)
		{
			PC->SelectActor(TeammateForce.Get());
			if (!Check(PC->GetInspectedForce() == TeammateForce.Get() && PC->GetSelectedBuilding() == nullptr,
					TEXT("Selecting a teammate force opens only its read-only inspection")))
				return true;
			const FVector Center = TeammateForce->GetCenter();
			// The supplied location is deliberately elsewhere: authority must resolve
			// the real teammate center, not trust the requested need-help coordinates.
			const FVector Spoofed = State->EnemyHeadquarters->GetActorLocation();
			if (!Check(!Center.Equals(Spoofed, 1.f), TEXT("Force-center probe differs from its supplied coordinates"))
				|| !Check(FCommandService::Ping(PC, Spoofed, TeammateForce.Get()).IsAccepted(),
					TEXT("Teammate force ping is accepted after the two-second cooldown"))
				|| !Delivery(PC, Sender, TEXT("ping_need_help"), Center, TeammateForce->ForceNumber, 2))
				return true;
			LastAccepted = RealNow;
			Stage = 2;
			return false;
		}
		if (Stage == 2)
		{
			PC->SelectActor(EnemyForce.Get());
			if (!Check(PC->GetInspectedForce() == TeammateForce.Get() && PC->GetSelectedBuilding() == nullptr,
					TEXT("An enemy click preserves the read-only teammate inspection without gaining owned control")))
				return true;
			const FVector Spot = Ground + FVector(100.f, 0.f, 0.f);
			if (!Check(FCommandService::Ping(PC, Spot, EnemyForce.Get()).IsAccepted(),
					TEXT("An enemy target still permits an ordinary spot ping"))
				|| !Delivery(PC, Sender, TEXT("ping_look_here"), Spot, 0, 3))
				return true;
			UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(State);
			if (!Check(Announcer != nullptr, TEXT("Objective history exists for priority regression")))
				return true;
			Announcer->Raise(TEXT("region_lost"), 0, Ground, {});
			LastAccepted = RealNow;
			Stage = 3;
			return false;
		}
		if (Stage == 3)
		{
			const FVector Spot = Ground + FVector(200.f, 0.f, 0.f);
			if (!RejectForeignWorld(PC)
				|| !Check(FCommandService::Ping(PC, Spot, OwnForce.Get()).IsAccepted(),
					TEXT("An owned force cannot impersonate a teammate need-help request"))
				|| !Delivery(PC, Sender, TEXT("ping_look_here"), Spot, 0, 4))
				return true;
			if (!CheckObjectivePriority(PC, State))
				return true;
			LastTime = PC->PingCommands->GetEvents().Last().ServerTime;
			Stage = 4;
			return false;
		}
		const float ServerNow = State->GetServerWorldTimeSeconds();
		if (!Counts(PC, 4))
			return true;
		if (ServerNow - LastTime < UPingCommandComponent::Lifetime)
		{
			bObservedActiveLast = true;
			return false;
		}
		if (!Check(bObservedActiveLast, TEXT("The newest ping was observed active before its six-second expiry"))
			|| !Check(ServerNow - FirstTime >= UPingCommandComponent::Lifetime,
				TEXT("The oldest marker's activity has ended after six server seconds")))
			return true;
		for (const FObjectiveEvent& Event : PC->PingCommands->GetEvents())
			if (!Check(ServerNow - Event.ServerTime >= UPingCommandComponent::Lifetime,
					TEXT("Every retained ping is inactive after six server seconds")))
				return true;
		Test->AddInfo(TEXT("Ping proof: exact ground location and sender, explained authoritative throttle, teammate center/force number, enemy/owned targets remain Look here, cross-world force rejected, team-only delivery and six-second activity expiry with retained history."));
		return true;
	}

private:
	bool CheckObjectivePriority(ACommandPlayerController* PC, ACommandGameState* State)
	{
		UObjectiveAnnouncer* Announcer = UObjectiveAnnouncer::Get(State);
		const FObjectiveEvent& Objective = Announcer->GetEvents().Last();
		if (!Check(Objective.ServerTime < PC->PingCommands->GetEvents().Last().ServerTime,
				TEXT("Objective-priority fixture has a newer live ping")))
			return false;
		CommandHUDPanels::FLayout Layout;
		Layout.Alerts = { 0.f, 0.f, 390.f, 64.f };
		const CommandHUDPanels::FContext Context = CommandHUDPanels::MakeContext(PC);
		int32 First = 0;
		int32 Count = 0;
		CommandHUDPanels::ForEachAlert(Context, Layout, [&](const FObjectiveEvent& Event, const CommandHUDPanels::FRect&, float) {
			if (Count++ == 0)
				First = Event.Sequence;
		});
		return Check(First == Objective.Sequence && Count == 1, TEXT("Newer accepted ping traffic cannot displace the only visible objective row"));
	}

	bool Check(bool Value, const TCHAR* Message)
	{
		if (!Value)
			Test->AddError(Message);
		return Value;
	}
	bool Fail(const TCHAR* Message)
	{
		Test->AddError(Message);
		return true;
	}
	bool Setup(UWorld* World, ACommandPlayerController* PC, ACommandGameState* State)
	{
		for (TActorIterator<AEnemyCommander> It(World); It; ++It)
			It->Destroy();
		for (ACommandBuilding* Building : State->Buildings)
			if (IsValid(Building) && Building->IsProducer())
				FCommandService::ConfigureProduction(Building->OwningPlayerState, Building,
					Building->bForceConfigured ? Building->ProductionRole : static_cast<EUnitRole>(255), false);
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
			It->Destroy();
		State->bVerificationIncomePaused = true;
		Ally = Participant(World, State, 0, 1, TEXT("Ping teammate"));
		Opponent = Participant(World, State, 5, 2, TEXT("Ping opponent"));
		if (!Ally.IsValid() || !Opponent.IsValid())
			return false;
		TeammateForce = ArmyTestSetup::SpawnGroup(World, Ally.Get(), 0,
			ArmyTestSetup::FromFriendlyHQ(State, 1100.f, 650.f, 100.f));
		OwnForce = ArmyTestSetup::SpawnGroup(World, PC, 0,
			ArmyTestSetup::FromFriendlyHQ(State, 650.f, 850.f, 100.f));
		EnemyForce = ArmyTestSetup::SpawnGroup(World, nullptr, -1, ArmyTestSetup::HostileStaging(State));
		if (!Check(TeammateForce.IsValid() && OwnForce.IsValid() && EnemyForce.IsValid(),
				TEXT("Real-unit teammate, owned and enemy force fixtures spawn")))
			return false;
		TeammateForce->ForceNumber = 7;
		for (AArmyGroup* Group : { TeammateForce.Get(), OwnForce.Get(), EnemyForce.Get() })
		{
			if (!Check(FCommandService::IssueForceOrder(Group->GetOwningPlayerState(), Group, EForceVerb::MoveHold,
					ArmyTestSetup::CurrentRegion(Group)).IsAccepted(), TEXT("Ping fixture accepts a real held-region verb")))
				return false;
			Group->SetActorTickEnabled(false);
			for (AArmyUnit* Unit : Group->GetUnits())
				Unit->SetActorTickEnabled(false);
		}
		return Check(PC->PingCommands && PC->PingCommands->GetEvents().IsEmpty()
				&& Ally->PingCommands->GetEvents().IsEmpty() && Opponent->PingCommands->GetEvents().IsEmpty(),
			TEXT("A fresh match begins without communication history"));
	}
	ACommandPlayerController* Participant(UWorld* World, ACommandGameState* State, int32 Team, int32 Index, const TCHAR* Name)
	{
		ACommandPlayerController* PC = World->SpawnActor<ACommandPlayerController>();
		ACommandPlayerState* Player = World->SpawnActor<ACommandPlayerState>();
		if (!Check(PC && Player, TEXT("Communication controller and wallet fixture spawn")))
			return nullptr;
		PC->SetPlayerState(Player);
		Player->TeamIndex = Team;
		Player->CommanderIndex = Index;
		Player->SetPlayerName(Name);
		State->AddPlayerState(Player);
		return PC;
	}
	bool Counts(ACommandPlayerController* PC, int32 Expected)
	{
		return Check(PC->PingCommands->GetEvents().Num() == Expected && Ally->PingCommands->GetEvents().Num() == Expected,
				   TEXT("Sender and teammate have exactly the accepted team pings"))
			&& Check(Opponent->PingCommands->GetEvents().IsEmpty(), TEXT("Opponent controller never receives a team ping"));
	}
	bool Delivery(ACommandPlayerController* PC, const ACommandPlayerState* Sender, FName Id, const FVector& Location, int32 Number, int32 Expected)
	{
		if (!Counts(PC, Expected))
			return false;
		for (ACommandPlayerController* Receiver : { PC, Ally.Get() })
		{
			const FObjectiveEventView Events = Receiver->PingCommands->GetEvents();
			const FObjectiveEvent& Event = Events.Last();
			if (!Check(Event.Id == Id && Event.Location.Equals(Location, .01f)
						&& Event.AffectedTeam == Sender->TeamIndex && Event.Sequence < 0
						&& (Expected == 1 || Event.Sequence < Events[Expected - 2].Sequence),
					TEXT("Delivered ping preserves exact resolved location, variant, team and unique negative feed identity"))
				|| !Check(Event.Forces.Num() == 1 && Event.Forces[0].CommanderIndex == Sender->CommanderIndex
						&& Event.Forces[0].TeamIndex == Sender->TeamIndex && Event.Forces[0].PlayerName == Sender->GetPlayerName()
						&& Event.Forces[0].ForceNumber == Number,
					TEXT("Delivered event identifies the actual sender and requested teammate force")))
				return false;
			if (!Check(Event.TargetForceOwnerName == (Number > 0 ? TeammateForce->GetOwningPlayerState()->GetPlayerName() : FString()),
					TEXT("Need-help delivery identifies the target's owner separately from the sender")))
				return false;
		}
		return true;
	}
	bool RejectForeignWorld(ACommandPlayerController* PC)
	{
		UWorld* Foreign = UWorld::CreateWorld(EWorldType::Game, false);
		if (!Check(Foreign != nullptr, TEXT("Cross-world force rejection fixture creates a separate world")))
			return false;
		AArmyGroup* Force = Foreign->SpawnActor<AArmyGroup>();
		if (!Force)
		{
			Foreign->DestroyWorld(false);
			return Check(false, TEXT("Cross-world group fixture spawns"));
		}
		// Even a foreign actor advertising the same friendly wallet must never
		// become an authoritative teammate-force target in this match.
		FArmyGroupSpawn Spawn;
		Spawn.OwningPlayerState = TeammateForce->GetOwningPlayerState();
		Force->Initialize(Spawn);
		const FCommandResult Result = FCommandService::Ping(PC, Ground, Force);
		Foreign->DestroyWorld(false);
		return Check(!Result.IsAccepted() && !Result.Message.IsEmpty(), TEXT("A foreign-world force is rejected with an explanation"))
			&& Counts(PC, 3);
	}

	FAutomationTestBase* Test;
	double Started;
	double LastAccepted = 0.;
	int32 Stage = 0;
	FVector Ground = FVector::ZeroVector;
	float FirstTime = 0.f;
	float LastTime = 0.f;
	bool bObservedActiveLast = false;
	TWeakObjectPtr<ACommandPlayerController> Ally;
	TWeakObjectPtr<ACommandPlayerController> Opponent;
	TWeakObjectPtr<AArmyGroup> TeammateForce;
	TWeakObjectPtr<AArmyGroup> EnemyForce;
	TWeakObjectPtr<AArmyGroup> OwnForce;
};
}

bool FPingPlacementExpiryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(PingScenarioTests::FPlacementExpiryScenario(this));
	return true;
}

#endif
