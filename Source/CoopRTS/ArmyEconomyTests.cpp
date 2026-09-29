#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "EnemyCommander.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyEconomyTest, "CoopRTS.Economy.CaptureIncomeRecovery",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Run alone in a fresh standalone Boot world; use actual actors, server capture,
// game-state income and the owned controller's paid purchase entry point.
class FArmyEconomyScenario : public IAutomationLatentCommand
{
public:
	explicit FArmyEconomyScenario(FAutomationTestBase* InTest) : Test(InTest), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		if (FPlatformTime::Seconds() - Started > 40.)
		{
			Test->AddError(TEXT("Economy world failed to initialize within 40 seconds"));
			return true;
		}
		UWorld* World = nullptr;
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (Context.World() && Context.World()->IsGameWorld() && Context.World()->GetNetMode() == NM_Standalone)
				{ World = Context.World(); break; }
		if (!World) return false;
		if (!bIsolated)
		{
			for (TActorIterator<AEnemyCommander> It(World); It; ++It) It->Destroy();
			bIsolated = true; // Preserve this scenario's controlled capture/contest experiment.
		}
		if (FPlatformTime::Seconds() - Started < 3.) return false; // Wait for Boot navmesh and AI initialization.
		ACommandPlayerController* Controller = nullptr;
		AArmyGroup* Groups[2] = {};
		AArmyGroup* Enemy = nullptr;
		for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
			if (It->IsLocalController()) { Controller = *It; break; }
		if (!Controller) return false;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
		{
			if (It->bOpposingArmy) Enemy = *It;
			else if (It->GetOwner() == Controller && It->ArmyIndex >= 0 && It->ArmyIndex < 2)
				Groups[It->ArmyIndex] = *It;
		}
		ACommandGameState* State = World->GetGameState<ACommandGameState>();
		ACommandPlayerState* Wallet = Controller->GetPlayerState<ACommandPlayerState>();
		if (!Groups[0] || !Groups[1] || !Enemy || !State || !Wallet || State->CaptureSites.Num() != 3) return false;
		if (!Check(Groups[0]->Units.Num() == 6 && Groups[1]->Units.Num() == 6 && Enemy->Units.Num() == 6
			&& State->ControlledResourceSites == 0 && State->GetIncomePerSecond() > 0,
			TEXT("Fresh world starts with intact armies and baseline income but no territory"))) return true;
		ACapturePoint* Resource = nullptr;
		ACapturePoint* Forward = nullptr;
		for (ACapturePoint* Site : State->CaptureSites)
		{
			if (!Check(IsValid(Site) && Site->ControllingTeam == -1,
				TEXT("Each Boot capture site begins neutral"))) return true;
			if (Site->SiteKind == ECaptureSiteKind::Resource && !Resource) Resource = Site;
			if (Site->SiteKind == ECaptureSiteKind::Reinforcement) Forward = Site;
		}
		if (!Check(Resource && Forward && Resource != Forward, TEXT("Boot exposes resource and forward sites"))) return true;

		// Spawn a second real player-state wallet into GameState's player array.
		ACommandPlayerState* AllyWallet = World->SpawnActor<ACommandPlayerState>();
		if (!Check(IsValid(AllyWallet) && AllyWallet != Wallet, TEXT("Independent teammate wallet exists"))) return true;
		State->AddPlayerState(AllyWallet);
		const int32 Baseline = State->GetIncomePerSecond();
		if (!Check(Wallet->GetIncomePerSecond() == Baseline && AllyWallet->GetIncomePerSecond() == Baseline,
			TEXT("Both wallets quote baseline income without territory"))) return true;
		int32 BeforeA = Wallet->Resources;
		int32 BeforeB = AllyWallet->Resources;
		State->Tick(2.f);
		if (!Check(Wallet->Resources == BeforeA + 2 * Baseline && AllyWallet->Resources == BeforeB + 2 * Baseline,
			TEXT("Zero-territory baseline actually pays both independent wallets"))) return true;

		AArmyUnit* Capturer = Groups[0]->Units[0];
		AArmyUnit* Contester = Enemy->Units[0];
		Capturer->SetActorLocation(Resource->GetActorLocation() + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		Resource->AdvanceCapture(4.f);
		if (!Check(Resource->CaptureProgress > 0.f && Resource->CaptureProgress < 1.f && Resource->ControllingTeam == -1,
			TEXT("One friendly unit advances a neutral site without premature ownership"))) return true;
		const float ProgressBeforeContest = Resource->CaptureProgress;
		Contester->SetActorLocation(Resource->GetActorLocation() + FVector(0.f, 180.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		Resource->AdvanceCapture(9.f);
		if (!Check(FMath::IsNearlyEqual(Resource->CaptureProgress, ProgressBeforeContest) && Resource->ControllingTeam == -1,
			TEXT("Opposed live armies freeze capture progress and ownership"))) return true;
		Contester->SetActorLocation(Enemy->HomeLocation, false, nullptr, ETeleportType::TeleportPhysics);
		Resource->AdvanceCapture(9.f);
		if (!Check(Resource->ControllingTeam == Groups[0]->TeamIndex && State->ControlledResourceSites == 1,
			TEXT("Uncontested friendly presence captures and publishes shared resource ownership"))) return true;
		const int32 WithSite = State->GetIncomePerSecond();
		if (!Check(WithSite > Baseline && Wallet->GetIncomePerSecond() == WithSite
			&& AllyWallet->GetIncomePerSecond() == WithSite,
			TEXT("Team-owned site raises both wallets' income without second player's capture credit"))) return true;
		BeforeA = Wallet->Resources;
		BeforeB = AllyWallet->Resources;
		State->Tick(2.f);
		if (!Check(Wallet->Resources == BeforeA + 2 * WithSite && AllyWallet->Resources == BeforeB + 2 * WithSite,
			TEXT("Resource-site income actually pays both separate wallets"))) return true;

		Capturer->SetActorLocation(Groups[0]->HomeLocation + FVector(220.f, -140.f, 0.f), false, nullptr, ETeleportType::TeleportPhysics);
		Contester->SetActorLocation(Resource->GetActorLocation() + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		Resource->AdvanceCapture(8.1f);
		if (!Check(Resource->ControllingTeam == -1 && State->ControlledResourceSites == 0,
			TEXT("Enemy presence neutralizes friendly territory before claiming it"))) return true;
		Resource->AdvanceCapture(8.1f);
		if (!Check(Resource->ControllingTeam == Enemy->TeamIndex && State->GetIncomePerSecond() == Baseline,
			TEXT("Continued enemy presence transfers ownership and removes team income"))) return true;
		Contester->SetActorLocation(Enemy->HomeLocation, false, nullptr, ETeleportType::TeleportPhysics);
		BeforeA = Wallet->Resources;
		BeforeB = AllyWallet->Resources;
		State->Tick(2.f);
		if (!Check(Wallet->Resources == BeforeA + 2 * Baseline && AllyWallet->Resources == BeforeB + 2 * Baseline,
			TEXT("Losing all sites preserves baseline payments to both wallets"))) return true;

		// A real lethal hit creates a missing siege role. Insufficient funds and a
		// distant resource site each reject via the same owning-controller RPC.
		AArmyGroup* Group = Groups[0];
		AArmyUnit* Casualty = Group->Units[4];
		Casualty->ReceiveAttack(Casualty->Health, Contester);
		if (!Check(Group->Units.Num() == 5 && !Group->Units.Contains(Casualty)
			&& Group->GetReinforcementCost() > 0, TEXT("Server death creates a priced siege vacancy"))) return true;
		const int32 Quote = Group->GetReinforcementCost();
		for (AArmyUnit* Unit : Group->Units)
			Unit->SetActorLocation(Resource->GetActorLocation() + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		if (!Check(!Group->CanReinforceAtCurrentLocation(), TEXT("Distant resource site is not a valid reinforcement source"))) return true;
		if (!RejectWithoutMutation(Controller, Group, Wallet, TEXT("Distant purchase rejection preserves wallet, roster and order"))) return true;
		for (AArmyUnit* Unit : Group->Units)
			Unit->SetActorLocation(Group->HomeLocation + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		if (!Check(Group->CanReinforceAtCurrentLocation(), TEXT("Living group can purchase near its base"))) return true;
		if (!Check(Wallet->Resources >= Quote && Wallet->TrySpend(Wallet->Resources - Quote + 1)
			&& Wallet->Resources == Quote - 1, TEXT("Authoritative wallet can enter below-quote state"))) return true;
		if (!RejectWithoutMutation(Controller, Group, Wallet, TEXT("Unaffordable purchase preserves wallet, roster and order"))) return true;
		// With no territory and insufficient resources, baseline eventually funds a replacement.
		for (int32 Tick = 0; Tick < 10 && Wallet->Resources < Quote; ++Tick) State->Tick(2.f);
		if (!Check(State->ControlledResourceSites == 0 && Wallet->Resources >= Quote,
			TEXT("Baseline alone eventually funds casualty recovery"))) return true;
		BeforeA = Wallet->Resources;
		Controller->ServerReinforce(Group);
		if (!Check(Group->Units.Num() == 6 && Wallet->Resources == BeforeA - Quote
			&& Group->GetReinforcementCost() == 0, TEXT("Owned RPC pays quoted price and restores exactly one vacancy"))) return true;
		if (!CheckRoles(Group)) return true;
		if (!RejectWithoutMutation(Controller, Group, Wallet, TEXT("Full roster cannot buy an obsolete seventh unit"))) return true;

		// A forward site is shared team territory, but it is a source only while owned.
		Capturer = Group->Units[0];
		Capturer->SetActorLocation(Forward->GetActorLocation() + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		Forward->AdvanceCapture(9.f);
		if (!Check(Forward->ControllingTeam == Group->TeamIndex && State->ForwardSiteTeam == Group->TeamIndex,
			TEXT("Uncontested forward capture publishes a shared reinforcement source"))) return true;
		for (AArmyUnit* Unit : Group->Units)
			Unit->SetActorLocation(Forward->GetActorLocation() + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);
		FVector Source;
		bool bBase = true;
		if (!Check(Group->GetReinforcementSource(Source, bBase) && !bBase
			&& FVector::Dist2D(Source, Forward->GetActorLocation()) < 5.f,
			TEXT("Owned forward site becomes a valid living-group source"))) return true;
		// Return the group home before rebuilding the other army; no fake seventh member.
		for (AArmyUnit* Unit : Group->Units)
			Unit->SetActorLocation(Group->HomeLocation + FVector(0.f, 0.f, 95.f), false, nullptr, ETeleportType::TeleportPhysics);

		AArmyGroup* Wiped = Groups[1];
		TArray<AArmyUnit*> Victims;
		for (AArmyUnit* Unit : Wiped->Units) Victims.Add(Unit);
		for (AArmyUnit* Unit : Victims) Unit->ReceiveAttack(Unit->Health, Contester);
		if (!Check(IsValid(Wiped) && Wiped->GetOwner() == Controller && Wiped->Units.IsEmpty()
			&& Wiped->CanReinforceAtCurrentLocation(), TEXT("Wiped owned group remains a rebuildable base actor"))) return true;
		const int32 RebuildQuote = Wiped->GetReinforcementCost();
		if (!Check(RebuildQuote > Quote && RebuildQuote > 0, TEXT("Empty-group quote covers all missing roles"))) return true;
		for (int32 Tick = 0; Tick < 30 && Wallet->Resources < RebuildQuote; ++Tick) State->Tick(2.f);
		if (!Check(Wallet->Resources >= RebuildQuote, TEXT("Baseline income eventually funds a full rebuild"))) return true;
		BeforeA = Wallet->Resources;
		Controller->ServerReinforce(Wiped);
		if (!Check(Wiped->Units.Num() == 6 && Wallet->Resources == BeforeA - RebuildQuote
			&& Wiped->GetReinforcementCost() == 0 && Wiped->Order == EArmyOrder::Hold,
			TEXT("Owned RPC rebuilds a wiped army at base for its full quote"))) return true;
		if (!CheckRoles(Wiped)) return true;
		for (AArmyUnit* Unit : Wiped->Units)
			if (!Check(FVector::Dist2D(Unit->GetActorLocation(), Wiped->HomeLocation) < 450.f,
				TEXT("Every rebuilt role spawns at its own base"))) return true;
		Test->AddInfo(TEXT("Economy passed: contested/neutral/owned capture, both baseline and shared-site wallet payments, atomic failures, paid exact-role replacement, full-roster rejection, owned forward eligibility and base rebuild."));
		return true;
	}

private:
	bool Check(bool Condition, const TCHAR* Message)
	{
		if (!Condition) Test->AddError(Message);
		return Condition;
	}

	bool CheckRoles(const AArmyGroup* Group)
	{
		bool Occupied[6] = {};
		int32 Counts[3] = {};
		for (const AArmyUnit* Unit : Group->Units)
		{
			if (!Check(IsValid(Unit) && Unit->IsAlive() && Unit->Group == Group
				&& Unit->CompositionSlot >= 0 && Unit->CompositionSlot < 6,
				TEXT("Each restored member is alive in its owned composition slot"))) return false;
			const EUnitRole Expected = Unit->CompositionSlot < 2 ? EUnitRole::Frontline
				: Unit->CompositionSlot < 4 ? EUnitRole::Ranged : EUnitRole::Siege;
			if (!Check(Unit->UnitRole == Expected && !Occupied[Unit->CompositionSlot]
				&& ++Counts[Unit->CompositionSlot / 2] <= 2,
				TEXT("Restored role occupies its unique fixed composition slot"))) return false;
			Occupied[Unit->CompositionSlot] = true;
		}
		return Check(Group->Units.Num() == 6 && Counts[0] == 2 && Counts[1] == 2 && Counts[2] == 2,
			TEXT("Complete army has two frontline, two ranged and two siege roles"));
	}

	bool RejectWithoutMutation(ACommandPlayerController* Controller, AArmyGroup* Group,
		ACommandPlayerState* Wallet, const TCHAR* Message)
	{
		const TArray<TObjectPtr<AArmyUnit>> Members = Group->Units;
		const int32 Balance = Wallet->Resources;
		const uint32 Serial = Group->OrderSerial;
		const EArmyOrder Order = Group->Order;
		const FVector Destination = Group->Destination;
		Controller->ServerReinforce(Group);
		return Check(Wallet->Resources == Balance && Group->Units == Members
			&& Group->OrderSerial == Serial && Group->Order == Order && Group->Destination.Equals(Destination), Message);
	}

	FAutomationTestBase* Test;
	double Started;
	bool bIsolated = false;
};

bool FArmyEconomyTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyEconomyScenario(this));
	return true;
}

#endif
