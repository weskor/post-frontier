#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"
#include "ArmyGroup.h"
#include "ArmyUnit.h"
#include "CapturePoint.h"
#include "CommandGameState.h"
#include "CommandPlayerController.h"
#include "CommandPlayerState.h"
#include "Engine/Engine.h"
#include "EngineUtils.h"
#include "HAL/PlatformTime.h"
#include "Headquarters.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyMatchVictoryTest, "CoopRTS.Match.VictoryRestart",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FArmyMatchDefeatTest, "CoopRTS.Match.DefeatRestart",
	EAutomationTestFlags::ClientContext | EAutomationTestFlags::EngineFilter)

// Each invocation owns a fresh Boot world. Health/position setup shortens the encounter;
// neither test sets the result or inflicts the lethal HQ hit itself.
class FArmyMatchScenario : public IAutomationLatentCommand
{
public:
	FArmyMatchScenario(FAutomationTestBase* InTest, bool bInVictory)
		: Test(InTest), bVictory(bInVictory), Started(FPlatformTime::Seconds()) {}

	virtual bool Update() override
	{
		const double Now = FPlatformTime::Seconds();
		if (Now - Started > 110.)
		{
			Test->AddError(FString::Printf(TEXT("Match %s timed out at stage %d"), bVictory ? TEXT("victory") : TEXT("defeat"), Stage));
			return true;
		}
		UWorld* World = GameWorld();
		if (!World) return false;
		if (Stage == 0) return Begin(World, Now);
		if (Stage == 4) return CheckRestart(World);
		if (!State.IsValid() || !Controller.IsValid() || !Friendly.IsValid() || !Enemy.IsValid()
			|| !FriendlyHQ.IsValid() || !EnemyHQ.IsValid())
		{
			Test->AddError(TEXT("Match actors disappeared before restart"));
			return true;
		}
		AHeadquarters* TargetHQ = bVictory ? EnemyHQ.Get() : FriendlyHQ.Get();
		const EMatchResult Expected = bVictory ? EMatchResult::Victory : EMatchResult::Defeat;
		if (Stage == 1)
		{
			if (Now - StageStarted < .2) return false;
			if (TargetHQ->Health < InitialHealth)
			{
				if (!Check(WeaponAttacks(Attacker.Get()) > InitialAttacks,
					TEXT("HQ health falls only after actual nearby unit weapon attacks"))) return true;
				bObservedWeaponDamage = true;
			}
			if (State->MatchResult == EMatchResult::Ongoing)
			{
				if (bVictory && Now - StageStarted > 2. && Friendly->Order != EArmyOrder::Attack)
					Controller->ServerIssueAttack(Friendly.Get(), TargetHQ->GetActorLocation(), TargetHQ);
				return false;
			}
			if (!Check(bObservedWeaponDamage && TargetHQ->Health == 0 && !TargetHQ->IsAlive()
				&& State->MatchResult == Expected && State->FriendlyHeadquarters == FriendlyHQ.Get()
				&& State->EnemyHeadquarters == EnemyHQ.Get(),
				TEXT("Real weapon damage destroys only the intended HQ and publishes the correct result"))) return true;
			if (!Check((bVictory ? FriendlyHQ : EnemyHQ)->IsAlive(),
				TEXT("Opposing HQ survives the completed match"))) return true;
			TerminalAttacks = WeaponAttacks(Attacker.Get());
			TerminalWallet = Wallet->Resources;
			TerminalSerial = Friendly->OrderSerial;
			TerminalEnemySerial = Enemy->OrderSerial;
			if (!bVictory)
				for (AArmyUnit* Unit : RecoveryGroup->Units)
					Unit->SetActorLocation(RecoveryGroup->HomeLocation + FVector(0.f, 0.f, 95.f),
						false, nullptr, ETeleportType::TeleportPhysics);
			if (!Check(RecoveryGroup->CanReinforceAtCurrentLocation()
				&& RecoveryGroup->GetReinforcementCost() <= Wallet->Resources,
				TEXT("Terminal N candidate is an affordable vacancy at a valid base"))) return true;
			TerminalUnits = RecoveryGroup->Units.Num();
			TerminalPosition = Attacker->GetCenter();
			// All command entry points must reject once terminal, including purchased units.
			Controller->ServerIssueOrder(Friendly.Get(), EArmyOrder::Move, FVector(-1800.f, -1600.f, 0.f));
			Controller->ServerIssueOrder(Friendly.Get(), EArmyOrder::Hold, FVector::ZeroVector);
			Controller->ServerIssueOrder(Friendly.Get(), EArmyOrder::Retreat, FVector::ZeroVector);
			Controller->ServerIssueAttack(Friendly.Get(), EnemyHQ->GetActorLocation(), EnemyHQ.Get());
			Controller->ServerReinforce(RecoveryGroup.Get());
			State->Tick(2.f);
			if (!Check(State->MatchResult == Expected && Friendly->OrderSerial == TerminalSerial
				&& RecoveryGroup->Units.Num() == TerminalUnits && Wallet->Resources == TerminalWallet,
				TEXT("Terminal move/hold/retreat/attack/purchase and income cannot mutate match state"))) return true;
			Stage = 2;
			StageStarted = Now;
			return false;
		}
		if (Stage == 2)
		{
			if (Now - StageStarted < 1.5) return false;
			if (!Check(State->MatchResult == Expected && TargetHQ->Health == 0
				&& Friendly->OrderSerial == TerminalSerial && Enemy->OrderSerial == TerminalEnemySerial
				&& WeaponAttacks(Attacker.Get()) == TerminalAttacks
				&& FVector::Dist2D(Attacker->GetCenter(), TerminalPosition) < 15.f,
				TEXT("Terminal result is stable; planner, combat and army movement stop"))) return true;
			OldWorld = World;
			Controller->ServerRequestRestart();
			Stage = 4;
			StageStarted = Now;
		}
		return false;
	}

private:
	static UWorld* GameWorld()
	{
		for (const FWorldContext& Context : GEngine->GetWorldContexts())
			if (UWorld* World = Context.World())
				if (World->IsGameWorld() && World->GetNetMode() == NM_Standalone) return World;
		return nullptr;
	}

	bool Check(bool Condition, const TCHAR* Message)
	{
		if (!Condition) Test->AddError(Message);
		return Condition;
	}

	static uint32 WeaponAttacks(const AArmyGroup* Group)
	{
		uint32 Count = 0;
		if (IsValid(Group))
			for (const AArmyUnit* Unit : Group->Units)
				if (IsValid(Unit)) Count += Unit->AttackCount;
		return Count;
	}

	bool Begin(UWorld* World, double Now)
	{
		if (Now - Started < 3.) return false;
		ACommandGameState* Found = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* Owner = nullptr;
		for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
			if (It->IsLocalController()) { Owner = *It; break; }
		if (!Found || !Owner || !Found->FriendlyHeadquarters || !Found->EnemyHeadquarters) return false;
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
		{
			if (It->bOpposingArmy) Enemy = *It;
			else if (It->GetOwner() == Owner && It->ArmyIndex == 0) Friendly = *It;
			else if (It->GetOwner() == Owner && It->ArmyIndex == 1) RecoveryGroup = *It;
		}
		if (!Friendly.IsValid() || !RecoveryGroup.IsValid() || !Enemy.IsValid()
			|| Friendly->Units.Num() != 6 || RecoveryGroup->Units.Num() != 6 || Enemy->Units.Num() != 6)
			return false;
		State = Found;
		Controller = Owner;
		Wallet = Owner->GetPlayerState<ACommandPlayerState>();
		FriendlyHQ = Found->FriendlyHeadquarters;
		EnemyHQ = Found->EnemyHeadquarters;
		if (!Check(Wallet.IsValid() && State->MatchResult == EMatchResult::Ongoing
			&& FriendlyHQ->IsAlive() && EnemyHQ->IsAlive() && FriendlyHQ != EnemyHQ
			&& FriendlyHQ->TeamIndex != EnemyHQ->TeamIndex,
			TEXT("Fresh world has two distinct living HQs and an ongoing result"))) return true;
		// A priced vacancy at a valid source makes terminal purchase rejection meaningful.
		AArmyUnit* Missing = RecoveryGroup->Units[4];
		Missing->ReceiveAttack(Missing->Health, Enemy->Units[0]);
		if (!Check(RecoveryGroup->Units.Num() == 5 && RecoveryGroup->GetReinforcementCost() <= Wallet->Resources,
			TEXT("Home group has an affordable missing siege role before terminal"))) return true;
		if (bVictory)
		{
			// The same owned server entry point handles static-floor area Attack and
			// refuses friendly HQs, arbitrary world actors and foreign-owned armies.
			const uint32 EnemySerialBeforeProbe = Enemy->OrderSerial;
			const uint32 BeforeArea = Friendly->OrderSerial;
			Controller->ServerIssueAttack(Friendly.Get(), Friendly->HomeLocation + FVector(0.f, 500.f, 0.f), nullptr);
			if (!Check(Friendly->OrderSerial > BeforeArea && Friendly->Order == EArmyOrder::Attack
				&& !Friendly->AttackTarget,
				TEXT("Null floor target creates an ordinary attack-location order"))) return true;
			const uint32 AreaSerial = Friendly->OrderSerial;
			Controller->ServerIssueAttack(Friendly.Get(), FriendlyHQ->GetActorLocation(), FriendlyHQ.Get());
			Controller->ServerIssueAttack(Friendly.Get(), State->CaptureSites[0]->GetActorLocation(), State->CaptureSites[0]);
			Controller->ServerIssueAttack(Enemy.Get(), FriendlyHQ->GetActorLocation(), FriendlyHQ.Get());
			if (!Check(Friendly->OrderSerial == AreaSerial && !Friendly->AttackTarget
				&& Enemy->OrderSerial == EnemySerialBeforeProbe,
				TEXT("Friendly HQ, unsupported site and foreign-owned enemy army reject target requests atomically"))) return true;
		}

		Attacker = bVictory ? Friendly : Enemy;
		AHeadquarters* TargetHQ = bVictory ? EnemyHQ.Get() : FriendlyHQ.Get();
		// Preserve a lethal weapon hit; directly editing the HQ health alone never ends a match.
		TargetHQ->Health = 60;
		InitialHealth = TargetHQ->Health;
		for (int32 Index = 0; Index < Attacker->Units.Num(); ++Index)
			Attacker->Units[Index]->SetActorLocation(TargetHQ->GetActorLocation()
				+ FVector(-230.f - 45.f * (Index % 3), (Index / 3) * 105.f - 55.f, 95.f),
				false, nullptr, ETeleportType::TeleportPhysics);
		if (bVictory)
		{
			// The owning player's real attack request drives this assault.
			Controller->ServerIssueAttack(Friendly.Get(), TargetHQ->GetActorLocation(), TargetHQ);
			if (!Check(Friendly->Order == EArmyOrder::Attack && Friendly->AttackTarget == TargetHQ,
				TEXT("Owner issues targeted HQ Attack instead of setting the match result"))) return true;
		}
		else
		{
			// An overwhelming intact enemy near an exposed HQ should choose the assault itself.
			// Move the defenders away; do not assign a plan or issue an enemy Attack here.
			for (TActorIterator<AArmyGroup> It(World); It; ++It)
				if (!It->bOpposingArmy)
					for (AArmyUnit* Unit : It->Units)
						Unit->SetActorLocation(FVector(3000.f, -2800.f, 95.f),
							false, nullptr, ETeleportType::TeleportPhysics);
		}
		InitialWallet = GetDefault<ACommandPlayerState>()->Resources;
		InitialAttacks = WeaponAttacks(Attacker.Get());
		Stage = 1;
		StageStarted = Now;
		return false;
	}

	bool FreshRoster(const AArmyGroup* Group)
	{
		bool Slots[6] = {};
		if (!Check(Group && Group->Units.Num() == 6, TEXT("Restart restores each six-member roster"))) return false;
		for (const AArmyUnit* Unit : Group->Units)
		{
			if (!Check(IsValid(Unit) && Unit->IsAlive() && Unit->Group == Group
				&& Unit->CompositionSlot >= 0 && Unit->CompositionSlot < 6
				&& !Slots[Unit->CompositionSlot]
				&& Unit->UnitRole == (Unit->CompositionSlot < 2 ? EUnitRole::Frontline
					: Unit->CompositionSlot < 4 ? EUnitRole::Ranged : EUnitRole::Siege),
				TEXT("Restart restores each unique role and slot"))) return false;
			Slots[Unit->CompositionSlot] = true;
		}
		return true;
	}

	bool CheckRestart(UWorld* World)
	{
		if (World == OldWorld.Get()) return false;
		ACommandGameState* Fresh = World->GetGameState<ACommandGameState>();
		ACommandPlayerController* Owner = nullptr;
		for (TActorIterator<ACommandPlayerController> It(World); It; ++It)
			if (It->IsLocalController()) { Owner = *It; break; }
		if (!Fresh || !Owner || !Fresh->FriendlyHeadquarters || !Fresh->EnemyHeadquarters
			|| Fresh->CaptureSites.Num() != 3) return false;
		ACommandPlayerState* FreshWallet = Owner->GetPlayerState<ACommandPlayerState>();
		AArmyGroup* FreshEnemy = nullptr;
		AArmyGroup* FreshGroups[2] = {};
		for (TActorIterator<AArmyGroup> It(World); It; ++It)
		{
			if (It->bOpposingArmy) FreshEnemy = *It;
			else if (It->GetOwner() == Owner && It->ArmyIndex >= 0 && It->ArmyIndex < 2)
				FreshGroups[It->ArmyIndex] = *It;
		}
		if (!FreshWallet || !FreshEnemy || !FreshGroups[0] || !FreshGroups[1]) return false;
		if (!Check(Fresh->MatchResult == EMatchResult::Ongoing
			&& Fresh->FriendlyHeadquarters != FriendlyHQ.Get() && Fresh->EnemyHeadquarters != EnemyHQ.Get()
			&& Fresh->FriendlyHeadquarters->Health == Fresh->FriendlyHeadquarters->MaxHealth()
			&& Fresh->EnemyHeadquarters->Health == Fresh->EnemyHeadquarters->MaxHealth()
			&& Fresh->ControlledResourceSites == 0 && Fresh->ForwardSiteTeam == -1
			&& FreshWallet->Resources == InitialWallet
			&& Fresh->EnemyResources >= GetDefault<ACommandGameState>()->EnemyResources
			&& Fresh->EnemyResources <= GetDefault<ACommandGameState>()->EnemyResources + 2 * Fresh->GetEnemyIncomePerSecond()
			&& Fresh->EnemyPlan != TEXT("MATCH COMPLETE"),
			TEXT("Restart creates fresh HQs, opening wallets, no territory, no stale terminal plan and ongoing result"))) return true;
		if (!FreshRoster(FreshEnemy) || !FreshRoster(FreshGroups[0]) || !FreshRoster(FreshGroups[1]))
			return true;
		for (const ACapturePoint* Site : Fresh->CaptureSites)
			if (!Check(IsValid(Site) && Site->ControllingTeam == -1
				&& FMath::IsNearlyZero(Site->CaptureProgress),
				TEXT("Restart resets each site's ownership and capture progress"))) return true;
		Test->AddInfo(TEXT("Weapon-caused terminal match, frozen gameplay and fresh Boot restart passed."));
		return true;
	}

	FAutomationTestBase* Test;
	const bool bVictory;
	const double Started;
	int32 Stage = 0;
	double StageStarted = 0.;
	int32 InitialHealth = 0;
	int32 InitialWallet = 0;
	int32 TerminalWallet = 0;
	int32 TerminalUnits = 0;
	uint32 InitialAttacks = 0;
	uint32 TerminalAttacks = 0;
	uint32 TerminalSerial = 0;
	uint32 TerminalEnemySerial = 0;
	bool bObservedWeaponDamage = false;
	FVector TerminalPosition = FVector::ZeroVector;
	TWeakObjectPtr<UWorld> OldWorld;
	TWeakObjectPtr<ACommandGameState> State;
	TWeakObjectPtr<ACommandPlayerController> Controller;
	TWeakObjectPtr<ACommandPlayerState> Wallet;
	TWeakObjectPtr<AArmyGroup> Friendly;
	TWeakObjectPtr<AArmyGroup> RecoveryGroup;
	TWeakObjectPtr<AArmyGroup> Enemy;
	TWeakObjectPtr<AArmyGroup> Attacker;
	TWeakObjectPtr<AHeadquarters> FriendlyHQ;
	TWeakObjectPtr<AHeadquarters> EnemyHQ;
};

bool FArmyMatchVictoryTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyMatchScenario(this, true));
	return true;
}

bool FArmyMatchDefeatTest::RunTest(const FString& Parameters)
{
	ADD_LATENT_AUTOMATION_COMMAND(FArmyMatchScenario(this, false));
	return true;
}

#endif
